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

// --- ASYNCHRONOUS TX QUEUE ENGINE ---

std::queue<std::vector<uint8_t>> tx_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

void tx_worker()
{
    while (true)
    {
        std::vector<uint8_t> packet;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !tx_queue.empty(); });
            packet = tx_queue.front();
            tx_queue.pop();
        }

        const uint8_t* ptr = packet.data();
        size_t remain = packet.size();
        while (remain > 0)
        {
            // This blocking call is now safely isolated in its own thread!
            int w = write(ep_in, ptr, remain);
            if (w < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                {
                    usleep(100);
                    continue;
                }
                LOG_E("USB TX Write Failed! " << strerror(errno));
                break; // Drop packet, but TX thread survives
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
    std::queue<std::vector<uint8_t>> empty;
    std::swap(tx_queue, empty);
}

void queue_packet(const std::vector<uint8_t>& packet)
{
    init_tx_thread();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        tx_queue.push(packet);
    }
    queue_cv.notify_one();
}

// --- PROTOCOL HELPERS ---

void queue_chunk(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    // If First Fragment (0x01) or Unencrypted First Fragment
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
    queue_packet(out);
}

void fragment_and_queue(uint8_t channel, bool is_encrypted, const std::vector<uint8_t>& payload_to_send)
{
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = payload_to_send.size(); // MUST be Ciphertext length when encrypted!

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = is_encrypted ? 0x0B : 0x03; // Unfragmented
        queue_chunk(payload_to_send, channel, flag, 0);
    }
    else
    {
        uint8_t base_flag = is_encrypted ? 0x08 : 0x00; // Fragmented Base Flag
        size_t offset = 0;

        while (offset < total_size)
        {
            size_t remain = total_size - offset;
            size_t chunk_size = std::min(remain, MAX_CHUNK);
            std::vector<uint8_t> chunk(payload_to_send.begin() + offset, payload_to_send.begin() + offset + chunk_size);

            uint8_t flag = base_flag;
            uint32_t unfrag_size = 0;

            if (offset == 0)
            {
                flag |= 0x01; // First Fragment
                unfrag_size = total_size;
            }
            else if (offset + chunk_size >= total_size)
            {
                flag |= 0x02; // Last Fragment
            }
            else
            {
                flag |= 0x00; // Middle Fragment
            }

            queue_chunk(chunk, channel, flag, unfrag_size);
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

    std::cout << "[DEBUG-TX] Unencrypted - Channel: " << (int)channel << " Type: " << type << " Size: " << out.size()
              << std::endl;

    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    queue_packet(out);
}

void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    // AAP lock serializes both Encryption AND the order it enters the Queue, maintaining perfect TLS Sequences
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);

    if (ssl_bypassed)
    {
        fragment_and_queue(channel, false, pt);
    }
    else
    {
        SSL_write(ssl, pt.data(), pt.size());
        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> ciphertext(pending);
            BIO_read(wbio, ciphertext.data(), pending);
            fragment_and_queue(channel, true, ciphertext);
        }
    }
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
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

            queue_packet(out);
        }
        else
        {
            uint16_t len_field = tls_record.size();
            std::vector<uint8_t> out;

            if ((encrypted_flag & 0x03) == 0x01)
            {
                len_field += 4;
            }

            out.push_back(target_channel);
            out.push_back(encrypted_flag);
            out.push_back((len_field >> 8) & 0xFF);
            out.push_back(len_field & 0xFF);

            if ((encrypted_flag & 0x03) == 0x01)
            {
                out.push_back((unfragmented_size >> 24) & 0xFF);
                out.push_back((unfragmented_size >> 16) & 0xFF);
                out.push_back((unfragmented_size >> 8) & 0xFF);
                out.push_back(unfragmented_size & 0xFF);
            }

            out.insert(out.end(), tls_record.begin(), tls_record.end());

            if (!(target_channel == 2 && ((encrypted_flag & 0x03) != 0x03 || encrypted_flag == 0x0B)))
            {
                std::cout << "[DEBUG-TX] Encrypted - Channel: " << (int)target_channel << " Flags: 0x" << std::hex
                          << (int)encrypted_flag << std::dec << " Size: " << out.size() << std::endl;
            }
            queue_packet(out);
        }
    }
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    std::cout << "[DEBUG] SEND Channel: " << (int)channel << " Type: " << type << " Size: " << serialized.size()
              << std::endl;

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
