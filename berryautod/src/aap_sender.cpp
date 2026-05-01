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

// --- LATE-ENCRYPTION TX QUEUE ---

struct TxPacket
{
    uint8_t channel;
    std::vector<uint8_t> payload;
    uint8_t fragment_flag;
    uint32_t unfragmented_size;
    bool is_control;
    bool encrypt;
    bool is_raw; // If true, payload is already formatted and encrypted. Write directly!
};

std::queue<TxPacket> high_priority_queue;
std::queue<TxPacket> low_priority_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

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

        std::vector<uint8_t> out;

        if (pkt.is_raw)
        {
            out = pkt.payload; // Bypass encryption entirely
        }
        else if (pkt.encrypt)
        {
            uint8_t base_flag = (pkt.is_control && pkt.channel != 0) ? 0x0C : 0x08;
            uint8_t flags = base_flag | pkt.fragment_flag;

            std::vector<uint8_t> ciphertext;
            {
                std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
                SSL_write(ssl, pkt.payload.data(), pkt.payload.size());
                int pending = BIO_ctrl_pending(wbio);
                if (pending > 0)
                {
                    ciphertext.resize(pending);
                    BIO_read(wbio, ciphertext.data(), pending);
                }
            }

            uint16_t len = ciphertext.size();
            if (pkt.fragment_flag == 0x01)
                len += 4;

            out.push_back(pkt.channel);
            out.push_back(flags);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);

            if (pkt.fragment_flag == 0x01)
            {
                out.push_back(pkt.unfragmented_size >> 24);
                out.push_back(pkt.unfragmented_size >> 16);
                out.push_back(pkt.unfragmented_size >> 8);
                out.push_back(pkt.unfragmented_size & 0xFF);
            }
            out.insert(out.end(), ciphertext.begin(), ciphertext.end());
        }
        else
        {
            uint8_t base_flag = (pkt.is_control && pkt.channel != 0) ? 0x04 : 0x00;
            uint8_t flags = base_flag | pkt.fragment_flag;

            uint16_t len = pkt.payload.size();
            if (pkt.fragment_flag == 0x01)
                len += 4;

            out.push_back(pkt.channel);
            out.push_back(flags);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);

            if (pkt.fragment_flag == 0x01)
            {
                out.push_back(pkt.unfragmented_size >> 24);
                out.push_back(pkt.unfragmented_size >> 16);
                out.push_back(pkt.unfragmented_size >> 8);
                out.push_back(pkt.unfragmented_size & 0xFF);
            }
            out.insert(out.end(), pkt.payload.begin(), pkt.payload.end());
        }

        const uint8_t* ptr = out.data();
        size_t remain = out.size();
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

void flush_ssl_buffers()
{
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    int pending = BIO_ctrl_pending(wbio);
    if (pending > 0)
    {
        std::vector<uint8_t> tls_record(pending);
        BIO_read(wbio, tls_record.data(), pending);

        std::vector<uint8_t> out;
        if (!is_tls_connected)
        {
            uint16_t len = tls_record.size() + 2;
            out.push_back(0);
            out.push_back(0x03);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);
            out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
            out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
            out.insert(out.end(), tls_record.begin(), tls_record.end());
        }
        else
        {
            uint16_t len = tls_record.size();
            out.push_back(0);
            out.push_back(0x0B);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);
            out.insert(out.end(), tls_record.begin(), tls_record.end());
        }

        TxPacket pkt;
        pkt.is_raw = true;
        pkt.payload = out;

        init_tx_thread();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            high_priority_queue.push(pkt);
        }
        queue_cv.notify_one();
    }
}

// --- PUBLIC API ---

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
    pkt.is_raw = true;
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

    std::vector<uint8_t> payload;
    payload.push_back((type >> 8) & 0xFF);
    payload.push_back(type & 0xFF);
    payload.insert(payload.end(), serialized.begin(), serialized.end());

    TxPacket pkt;
    pkt.channel = channel;
    pkt.payload = payload;
    pkt.fragment_flag = 0x03; // Unfragmented
    pkt.unfragmented_size = 0;
    pkt.is_control = (type >= 1 && type <= 26);
    pkt.encrypt = true;
    pkt.is_raw = false;

    init_tx_thread();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        high_priority_queue.push(pkt);
    }
    queue_cv.notify_one();
}

bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    uint32_t total_size = pt.size();
    const size_t MAX_CHUNK = 15000;

    init_tx_thread();
    std::lock_guard<std::mutex> lock(queue_mutex);

    if (channel == 2 && low_priority_queue.size() > 50)
    {
        return false;
    }

    if (total_size <= MAX_CHUNK)
    {
        TxPacket pkt;
        pkt.channel = channel;
        pkt.payload = pt;
        pkt.fragment_flag = 0x03;
        pkt.unfragmented_size = 0;
        pkt.is_control = false;
        pkt.encrypt = !ssl_bypassed;
        pkt.is_raw = false;

        if (channel == 2)
            low_priority_queue.push(pkt);
        else
            high_priority_queue.push(pkt);
    }
    else
    {
        size_t offset = 0;
        while (offset < total_size)
        {
            size_t chunk_size = std::min(total_size - offset, MAX_CHUNK);
            std::vector<uint8_t> chunk(pt.begin() + offset, pt.begin() + offset + chunk_size);

            TxPacket pkt;
            pkt.channel = channel;
            pkt.payload = chunk;

            if (offset == 0)
            {
                pkt.fragment_flag = 0x01;
                pkt.unfragmented_size = total_size;
            }
            else if (offset + chunk_size >= total_size)
            {
                pkt.fragment_flag = 0x02;
                pkt.unfragmented_size = 0;
            }
            else
            {
                pkt.fragment_flag = 0x00;
                pkt.unfragmented_size = 0;
            }

            pkt.is_control = false;
            pkt.encrypt = !ssl_bypassed;
            pkt.is_raw = false;

            if (channel == 2)
                low_priority_queue.push(pkt);
            else
                high_priority_queue.push(pkt);

            offset += chunk_size;
        }
    }

    queue_cv.notify_all();
    return true;
}
