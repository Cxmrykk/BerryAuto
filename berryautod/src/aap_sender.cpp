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

// STRICT SINGLE-FIFO QUEUE: Guarantees TLS Sequence numbers perfectly match USB transmission order!
std::queue<std::vector<std::vector<uint8_t>>> tx_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

void tx_worker()
{
    while (true)
    {
        std::vector<std::vector<uint8_t>> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !tx_queue.empty(); });

            batch = tx_queue.front();
            tx_queue.pop();
        }

        for (const auto& chunk : batch)
        {
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
}

void init_tx_thread()
{
    std::call_once(tx_thread_flag, [] { std::thread(tx_worker).detach(); });
}

void flush_usb_tx_queue()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::queue<std::vector<std::vector<uint8_t>>> empty;
    std::swap(tx_queue, empty);
}

int get_tx_queue_size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tx_queue.size();
}

void build_chunk(std::vector<std::vector<uint8_t>>& batch, const std::vector<uint8_t>& pt, uint8_t target_channel,
                 uint8_t flags, uint32_t unfragmented_size)
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
    batch.push_back(std::move(out));
}

void fragment_and_batch(uint8_t channel, bool is_encrypted, const std::vector<uint8_t>& payload,
                        std::vector<std::vector<uint8_t>>& batch)
{
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = payload.size();

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = is_encrypted ? 0x0B : 0x03;
        build_chunk(batch, payload, channel, flag, 0);
    }
    else
    {
        uint8_t base_flag = is_encrypted ? 0x08 : 0x00;
        size_t offset = 0;

        while (offset < total_size)
        {
            size_t remain = total_size - offset;
            size_t chunk_size = std::min(remain, MAX_CHUNK);
            std::vector<uint8_t> chunk(payload.begin() + offset, payload.begin() + offset + chunk_size);

            uint8_t flag = base_flag;
            uint32_t unfrag_size = 0;

            if (offset == 0)
            {
                flag |= 0x01;
                unfrag_size = total_size; // EXACT CIPHERTEXT SIZE. Prevents -251 errors!
            }
            else if (offset + chunk_size >= total_size)
            {
                flag |= 0x02;
            }
            else
            {
                flag |= 0x00;
            }

            build_chunk(batch, chunk, channel, flag, unfrag_size);
            offset += chunk_size;
        }
    }
}

void flush_ssl_buffers()
{
    init_tx_thread();

    // SIMULTANEOUS LOCK: Enforces flawless TLS ordering!
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    std::lock_guard<std::mutex> tx_lock(queue_mutex);

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

        std::vector<std::vector<uint8_t>> batch = {out};
        tx_queue.push(std::move(batch));
        queue_cv.notify_one();
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

    init_tx_thread();
    std::lock_guard<std::mutex> tx_lock(queue_mutex);
    std::vector<std::vector<uint8_t>> batch = {out};
    tx_queue.push(std::move(batch));
    queue_cv.notify_one();
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

    std::vector<std::vector<uint8_t>> batch;

    init_tx_thread();
    {
        std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
        std::lock_guard<std::mutex> tx_lock(queue_mutex);

        std::vector<uint8_t> pt;
        pt.push_back((type >> 8) & 0xFF);
        pt.push_back(type & 0xFF);
        pt.insert(pt.end(), serialized.begin(), serialized.end());

        SSL_write(ssl, pt.data(), pt.size());
        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> ciphertext(pending);
            BIO_read(wbio, ciphertext.data(), pending);

            uint8_t flag = 0x0B;
            if (channel != 0 && type <= 26)
                flag = 0x0F;

            build_chunk(batch, ciphertext, channel, flag, 0);
        }

        if (!batch.empty())
        {
            tx_queue.push(std::move(batch));
            queue_cv.notify_one();
        }
    }
}

bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    init_tx_thread();
    std::vector<std::vector<uint8_t>> batch;

    {
        std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
        std::lock_guard<std::mutex> tx_lock(queue_mutex);

        // PRE-EMPTIVE DROP: Protects Pings! If the FIFO is backed up, reject video instantly!
        if (channel == 2 && tx_queue.size() >= 1)
            return false;

        if (ssl_bypassed)
        {
            fragment_and_batch(channel, false, pt, batch);
        }
        else
        {
            SSL_write(ssl, pt.data(), pt.size());
            int pending = BIO_ctrl_pending(wbio);
            if (pending > 0)
            {
                std::vector<uint8_t> ciphertext(pending);
                BIO_read(wbio, ciphertext.data(), pending);
                fragment_and_batch(channel, true, ciphertext, batch);
            }
        }

        if (!batch.empty())
        {
            tx_queue.push(std::move(batch));
            queue_cv.notify_one();
        }
    }

    return true;
}
