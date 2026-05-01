#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <errno.h>
#include <mutex>
#include <queue>
#include <string.h>
#include <thread>
#include <unistd.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

// --- STRICT FIFO TX QUEUE (PRESERVES TLS SEQUENCE) ---

std::queue<std::vector<std::vector<uint8_t>>> tx_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;
std::atomic<bool> tx_active{false}; // Tracks if the hardware is actively blocked on write()

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

        tx_active = true; // Mark hardware as busy

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

        tx_active = false; // Hardware is free
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
    tx_active = false;
}

bool is_tx_busy()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return !tx_queue.empty() || tx_active.load();
}

void enqueue_batch(std::vector<std::vector<uint8_t>>& batch)
{
    if (batch.empty())
        return;

    init_tx_thread();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        tx_queue.push(std::move(batch));
    }
    queue_cv.notify_one();
}

// --- PROTOCOL HELPERS ---

void build_chunk(std::vector<std::vector<uint8_t>>& batch, const std::vector<uint8_t>& pt, uint8_t target_channel,
                 uint8_t flags, uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    if ((flags & 0x03) == 0x01)
    {
        len_field += 4;
    }

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
                unfrag_size = total_size; // EXACT CIPHERTEXT SIZE. Precludes -251 Errors.
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

// --- PUBLIC API ---

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload)
{
    uint16_t len_field = payload.size() + 2;
    std::vector<uint8_t> out;
    out.push_back(channel);
    out.push_back(flags);
    out.push_back((len_field >> 8) & 0xFF);
    out.push_back(len_field & 0xFF);
    out.push_back((type >> 8) & 0xFF);
    out.push_back(type & 0xFF);
    out.insert(out.end(), payload.begin(), payload.end());

    std::vector<std::vector<uint8_t>> batch = {out};
    enqueue_batch(batch);
}

void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    std::vector<std::vector<uint8_t>> batch;

    {
        // STRICT LOCK: Serializes TLS Encryption AND Queue Insertion.
        std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);

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

        enqueue_batch(batch); // Inserted while holding AAP lock = Guaranteed strict sequence!
    }
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
{
    std::vector<std::vector<uint8_t>> batch;

    {
        std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);

        if (!pt.empty())
        {
            SSL_write(ssl, pt.data(), pt.size());
        }

        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> tls_record(pending);
            BIO_read(wbio, tls_record.data(), pending);

            if (!is_tls_connected)
            {
                uint16_t len_field = tls_record.size() + 2;
                std::vector<uint8_t> out;
                out.push_back(0);
                out.push_back(0x03);
                out.push_back((len_field >> 8) & 0xFF);
                out.push_back(len_field & 0xFF);
                out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
                out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
                out.insert(out.end(), tls_record.begin(), tls_record.end());

                batch.push_back(std::move(out));
            }
            else
            {
                build_chunk(batch, tls_record, target_channel, encrypted_flag, unfragmented_size);
            }
        }

        enqueue_batch(batch);
    }
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    bool is_control = (type >= 1 && type <= 26);

    if (ssl_bypassed)
    {
        uint8_t flags = 0x03; // Base Unencrypted
        if (channel != 0 && is_control)
            flags = 0x07;

        send_unencrypted(channel, flags, type, serialized);
    }
    else
    {
        uint8_t flags = 0x0B; // Base Encrypted
        if (channel != 0 && is_control)
            flags = 0x0F;

        std::vector<uint8_t> plaintext;
        plaintext.push_back((type >> 8) & 0xFF);
        plaintext.push_back(type & 0xFF);
        plaintext.insert(plaintext.end(), serialized.begin(), serialized.end());

        ssl_write_and_flush_unlocked(plaintext, channel, flags, 0);
    }
}
