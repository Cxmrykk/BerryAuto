#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <errno.h>
#include <mutex>
#include <string.h>
#include <unistd.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

std::mutex usb_tx_mutex;

// Helper: Pushes bytes to fd. Caller MUST hold usb_tx_mutex to prevent interleaving.
void write_to_usb_unlocked(const std::vector<uint8_t>& data)
{
    const uint8_t* ptr = data.data();
    size_t remain = data.size();
    while (remain > 0)
    {
        int w = write(ep_in, ptr, remain);
        if (w < 0)
        {
            // Protect against Linux kernel interrupts causing dropped chunks
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

// Helper: Builds AAP Header. Caller MUST hold usb_tx_mutex.
void write_chunk_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags,
                          uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    if ((flags & 0x03) == 0x01) // If First Fragment, add 4 bytes for unfragmented_size
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
    write_to_usb_unlocked(out);
}

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

    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);
    write_to_usb_unlocked(out);
}

void aap_send_raw(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);
    write_chunk_unlocked(pt, target_channel, flags, unfragmented_size);
}

void fragment_and_send(uint8_t channel, bool is_encrypted, const std::vector<uint8_t>& full_payload)
{
    // Max chunk safely below 16384 Android limit
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = full_payload.size();

    // LOCK ENTIRE MESSAGE: Prevents Pings from interleaving between video fragments!
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = is_encrypted ? 0x0B : 0x03;
        write_chunk_unlocked(full_payload, channel, flag, 0);
    }
    else
    {
        uint8_t base_flag = is_encrypted ? 0x08 : 0x00;
        size_t offset = 0;

        while (offset < total_size)
        {
            size_t remain = total_size - offset;
            size_t chunk_size = std::min(remain, MAX_CHUNK);
            std::vector<uint8_t> chunk(full_payload.begin() + offset, full_payload.begin() + offset + chunk_size);

            uint8_t flag = base_flag;
            uint32_t unfrag_size = 0;

            if (offset == 0)
            {
                flag |= 0x01; // First
                unfrag_size = total_size;
            }
            else if (offset + chunk_size >= total_size)
            {
                flag |= 0x02; // Last
            }
            else
            {
                flag |= 0x00; // Middle
            }

            write_chunk_unlocked(chunk, channel, flag, unfrag_size);
            offset += chunk_size;
        }
    }
}

void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    if (ssl_bypassed)
    {
        fragment_and_send(channel, false, pt);
    }
    else
    {
        std::vector<uint8_t> ciphertext;
        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            SSL_write(ssl, pt.data(), pt.size());
            int pending = BIO_ctrl_pending(wbio);
            if (pending > 0)
            {
                ciphertext.resize(pending);
                BIO_read(wbio, ciphertext.data(), pending);
            }
        }

        if (!ciphertext.empty())
        {
            fragment_and_send(channel, true, ciphertext);
        }
    }
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
{
    std::vector<std::vector<uint8_t>> out_packets;
    {
        std::lock_guard<std::recursive_mutex> lock(aap_mutex);

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
                out_packets.push_back(out);
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
                out_packets.push_back(out);
            }
        }
    }

    // LOCK ENTIRE BATCH: Prevents USB interleaving
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);
    for (const auto& pkt : out_packets)
    {
        uint8_t channel = pkt[0];
        uint8_t flag = pkt[1];
        if (!(channel == 2 && ((flag & 0x03) != 0x03 || flag == 0x0B)))
        {
            std::cout << "[DEBUG-TX] Encrypted - Channel: " << (int)channel << " Flags: 0x" << std::hex << (int)flag
                      << std::dec << " Size: " << pkt.size() << std::endl;
        }
        write_to_usb_unlocked(pkt);
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
