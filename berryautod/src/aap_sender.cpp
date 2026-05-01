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

// The queue now holds INDIVIDUAL FRAGMENTS (raw bytes ready for the wire).
// This allows a Ping (High Priority) to be popped and sent between Video Fragments!
std::queue<std::vector<uint8_t>> high_priority_queue;
std::queue<std::vector<uint8_t>> low_priority_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

void tx_worker()
{
    while (true)
    {
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !high_priority_queue.empty() || !low_priority_queue.empty(); });

            // PRIORITY DEMUX: Always process Pings/Control first!
            if (!high_priority_queue.empty())
            {
                chunk = high_priority_queue.front();
                high_priority_queue.pop();
            }
            else
            {
                chunk = low_priority_queue.front();
                low_priority_queue.pop();
            }
        }

        const uint8_t* ptr = chunk.data();
        size_t remain = chunk.size();
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
    std::queue<std::vector<uint8_t>> empty1;
    std::queue<std::vector<uint8_t>> empty2;
    std::swap(high_priority_queue, empty1);
    std::swap(low_priority_queue, empty2);
}

int get_media_tx_queue_size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return low_priority_queue.size();
}

void enqueue_chunk(uint8_t channel, std::vector<uint8_t> chunk)
{
    init_tx_thread();
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (channel == 2)
        low_priority_queue.push(std::move(chunk));
    else
        high_priority_queue.push(std::move(chunk));
    queue_cv.notify_one();
}

void build_and_enqueue_chunk(uint8_t channel, uint8_t flags, uint32_t unfragmented_size, const std::vector<uint8_t>& pt)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    if ((flags & 0x03) == 0x01)
        len_field += 4;

    out.push_back(channel);
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
    enqueue_chunk(channel, out);
}

void fragment_and_enqueue(uint8_t channel, bool is_encrypted, bool is_control, const std::vector<uint8_t>& payload)
{
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = payload.size();

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = 0;
        if (is_encrypted)
            flag = (is_control && channel != 0) ? 0x0F : 0x0B;
        else
            flag = (is_control && channel != 0) ? 0x07 : 0x03;
        build_and_enqueue_chunk(channel, flag, 0, payload);
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

            build_and_enqueue_chunk(channel, flag, unfrag, chunk);
            offset += chunk_size;
        }
    }
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
        enqueue_chunk(0, out);
    }
}

void send_unencrypted(uint8_t channel, uint16_t type, const std::vector<uint8_t>& payload)
{
    uint8_t flags = 0x03;
    if (channel != 0 && type <= 26)
        flags = 0x07;

    uint16_t len_field = payload.size() + 2;
    std::vector<uint8_t> out;
    out.push_back(channel);
    out.push_back(flags);
    out.push_back((len_field >> 8) & 0xFF);
    out.push_back(len_field & 0xFF);
    out.push_back((type >> 8) & 0xFF);
    out.push_back(type & 0xFF);
    out.insert(out.end(), payload.begin(), payload.end());
    enqueue_chunk(channel, out);
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    if (ssl_bypassed)
    {
        send_unencrypted(channel, type, serialized);
        return;
    }

    std::vector<uint8_t> pt;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), serialized.begin(), serialized.end());

    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    SSL_write(ssl, pt.data(), pt.size());
    int pending = BIO_ctrl_pending(wbio);
    if (pending > 0)
    {
        std::vector<uint8_t> ciphertext(pending);
        BIO_read(wbio, ciphertext.data(), pending);
        fragment_and_enqueue(channel, true, (type <= 26), ciphertext);
    }
}

bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    // BACKPRESSURE: 1 I-Frame creates ~10 fragments.
    // If there are >30 chunks waiting (~3 frames), drop the frame natively to prevent pipe choking!
    if (channel == 2 && get_media_tx_queue_size() > 30)
        return false;

    uint16_t type = (pt[0] << 8) | pt[1];

    // THE HOLY GRAIL FIX: RAW VIDEO DATA (Type 0 and 1) IS NEVER ENCRYPTED!
    bool should_encrypt = (!ssl_bypassed) && (type >= 0x8000);

    if (!should_encrypt)
    {
        fragment_and_enqueue(channel, false, false, pt);
    }
    else
    {
        std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
        SSL_write(ssl, pt.data(), pt.size());
        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> ciphertext(pending);
            BIO_read(wbio, ciphertext.data(), pending);
            fragment_and_enqueue(channel, true, false, ciphertext);
        }
    }
    return true;
}
