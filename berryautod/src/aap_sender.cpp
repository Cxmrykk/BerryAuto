#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <condition_variable>
#include <errno.h>
#include <mutex>
#include <queue>
#include <string.h>
#include <thread>
#include <unistd.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

struct TxPacket
{
    uint8_t channel;
    std::vector<uint8_t> payload;
    bool is_control;
    bool is_raw_ssl; // If true, payload is already encrypted/formatted. Bypass processing.
};

std::queue<TxPacket> high_priority_queue;
std::queue<TxPacket> low_priority_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

void write_buffer(const std::vector<uint8_t>& data)
{
    const uint8_t* ptr = data.data();
    size_t remain = data.size();
    while (remain > 0)
    {
        int w = write(ep_in, ptr, remain);
        if (w < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                usleep(100);
                continue;
            }
            LOG_E("USB TX Write Failed! " << strerror(errno));
            break;
        }
        ptr += w;
        remain -= w;
    }
}

void write_chunk(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    if ((flags & 0x03) == 0x01)
        len_field += 4;

    out.push_back(target_channel);
    out.push_back(flags);
    out.push_back((len_field >> 8) & 0xFF);
    out.push_back(len_field & 0xFF);

    if ((flags & 0x03) == 0x01)
    {
        out.push_back((unfragmented_size >> 24) & 0xFF);
        out.push_back((unfragmented_size >> 16) & 0xFF);
        out.push_back((unfragmented_size >> 8) & 0xFF);
        out.push_back(unfragmented_size & 0xFF);
    }

    out.insert(out.end(), pt.begin(), pt.end());
    write_buffer(out);
}

void transmit_atomic(uint8_t channel, bool is_encrypted, const std::vector<uint8_t>& payload, bool is_control)
{
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = payload.size(); // MUST BE CIPHERTEXT SIZE IF ENCRYPTED (Prevents -251 Buffer Overflow)

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = 0;
        if (is_encrypted)
            flag = (is_control && channel != 0) ? 0x0F : 0x0B;
        else
            flag = (is_control && channel != 0) ? 0x07 : 0x03;

        write_chunk(payload, channel, flag, 0);
    }
    else
    {
        uint8_t base_flag = 0;
        if (is_encrypted)
            base_flag = (is_control && channel != 0) ? 0x0C : 0x08;
        else
            base_flag = (is_control && channel != 0) ? 0x04 : 0x00;

        size_t offset = 0;
        while (offset < total_size)
        {
            size_t remain = total_size - offset;
            size_t chunk_size = std::min(remain, MAX_CHUNK);
            std::vector<uint8_t> chunk(payload.begin() + offset, payload.begin() + offset + chunk_size);

            uint8_t flag = base_flag;
            uint32_t unfrag = 0;
            if (offset == 0)
            {
                flag |= 0x01;
                unfrag = total_size;
            }
            else if (offset + chunk_size >= total_size)
            {
                flag |= 0x02;
            }
            else
            {
                flag |= 0x00;
            }

            write_chunk(chunk, channel, flag, unfrag);
            offset += chunk_size;
        }
    }
}

void tx_worker()
{
    while (true)
    {
        TxPacket pkt;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !high_priority_queue.empty() || !low_priority_queue.empty(); });

            if (!high_priority_queue.empty())
            {
                pkt = high_priority_queue.front();
                high_priority_queue.pop();
            }
            else
            {
                pkt = low_priority_queue.front();
                low_priority_queue.pop();
            }
        }

        if (pkt.is_raw_ssl)
        {
            write_buffer(pkt.payload); // Handshake bypass
        }
        else
        {
            if (ssl_bypassed)
            {
                transmit_atomic(pkt.channel, false, pkt.payload, pkt.is_control);
            }
            else
            {
                std::vector<uint8_t> ciphertext;
                {
                    // LATE ENCRYPTION: Guarantees TLS Sequence matches physical wire transmission order!
                    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
                    SSL_write(ssl, pkt.payload.data(), pkt.payload.size());
                    int pending = BIO_ctrl_pending(wbio);
                    if (pending > 0)
                    {
                        ciphertext.resize(pending);
                        BIO_read(wbio, ciphertext.data(), pending);
                    }
                }
                if (!ciphertext.empty())
                {
                    transmit_atomic(pkt.channel, true, ciphertext, pkt.is_control);
                }
            }
        }
    }
}

void init_tx_thread()
{
    std::call_once(tx_thread_flag, [] { std::thread(tx_worker).detach(); });
}

void flush_usb_tx_queue()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::queue<TxPacket> empty1;
    std::queue<TxPacket> empty2;
    std::swap(high_priority_queue, empty1);
    std::swap(low_priority_queue, empty2);
}

int get_media_tx_queue_size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return low_priority_queue.size();
}

void flush_ssl_buffers()
{
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    int pending = BIO_ctrl_pending(wbio);
    if (pending > 0)
    {
        std::vector<uint8_t> tls_record(pending);
        BIO_read(wbio, tls_record.data(), pending);

        std::vector<uint8_t> out;
        uint16_t len = tls_record.size();

        if (!is_tls_connected)
        {
            out.push_back(0);
            out.push_back(0x03);
            out.push_back((len + 2) >> 8);
            out.push_back((len + 2) & 0xFF);
            out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
            out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
        }
        else
        {
            out.push_back(0);
            out.push_back(0x0B);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);
        }
        out.insert(out.end(), tls_record.begin(), tls_record.end());

        TxPacket pkt;
        pkt.is_raw_ssl = true;
        pkt.payload = out;

        init_tx_thread();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            high_priority_queue.push(pkt);
        }
        queue_cv.notify_one();
    }
}

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload)
{
    uint16_t len = payload.size() + 2;
    std::vector<uint8_t> out;
    out.push_back(channel);
    out.push_back(flags);
    out.push_back(len >> 8);
    out.push_back(len & 0xFF);
    out.push_back((type >> 8) & 0xFF);
    out.push_back(type & 0xFF);
    out.insert(out.end(), payload.begin(), payload.end());

    TxPacket pkt;
    pkt.is_raw_ssl = true;
    pkt.payload = out;

    init_tx_thread();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        high_priority_queue.push(pkt);
    }
    queue_cv.notify_one();
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    if (ssl_bypassed)
    {
        uint8_t flags = 0x03;
        bool is_control = (type >= 1 && type <= 26);
        if (channel != 0 && is_control)
            flags = 0x07;
        send_unencrypted(channel, flags, type, serialized);
        return;
    }

    std::vector<uint8_t> pt;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), serialized.begin(), serialized.end());

    TxPacket pkt;
    pkt.channel = channel;
    pkt.payload = pt;
    pkt.is_control = (type <= 26);
    pkt.is_raw_ssl = false;

    init_tx_thread();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        high_priority_queue.push(pkt);
    }
    queue_cv.notify_one();
}

bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    init_tx_thread();
    std::lock_guard<std::mutex> lock(queue_mutex);

    // BACKPRESSURE: Drop video if the queue is backed up
    if (channel == 2 && low_priority_queue.size() > 5)
        return false;

    TxPacket pkt;
    pkt.channel = channel;
    pkt.payload = pt;
    pkt.is_control = false;
    pkt.is_raw_ssl = false;

    if (channel == 2)
        low_priority_queue.push(pkt);
    else
        high_priority_queue.push(pkt);

    queue_cv.notify_one();
    return true;
}
