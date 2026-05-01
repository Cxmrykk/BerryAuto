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

// --- INTERNAL HELPERS (Must be called with BOTH locks held) ---

void write_to_usb_unlocked(const std::vector<uint8_t>& data)
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

void write_chunk_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags,
                          uint32_t unfragmented_size)
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
    write_to_usb_unlocked(out);
}

void fragment_and_send_unlocked(uint8_t channel, bool is_encrypted, const std::vector<uint8_t>& payload_to_send,
                                uint32_t original_plaintext_size)
{
    const size_t MAX_CHUNK = 15000;
    uint32_t total_size = payload_to_send.size(); // Ciphertext length

    if (total_size <= MAX_CHUNK)
    {
        uint8_t flag = is_encrypted ? 0x0B : 0x03; // Unfragmented
        write_chunk_unlocked(payload_to_send, channel, flag, 0);
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
                flag |= 0x01;                          // First Fragment
                unfrag_size = original_plaintext_size; // THE MAGIC FIX: Pass Plaintext size, not Ciphertext size!
            }
            else if (offset + chunk_size >= total_size)
            {
                flag |= 0x02; // Last Fragment
            }
            else
            {
                flag |= 0x00; // Middle Fragment
            }

            write_chunk_unlocked(chunk, channel, flag, unfrag_size);
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
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);
    write_to_usb_unlocked(out);
}

void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    uint32_t original_plaintext_size = pt.size();

    // STRICT LOCKING: Hold both locks for the ENTIRE duration of the fragmented frame.
    // This absolutely guarantees that a Ping Response cannot interleave between video chunks!
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);

    if (ssl_bypassed)
    {
        fragment_and_send_unlocked(channel, false, pt, original_plaintext_size);
    }
    else
    {
        SSL_write(ssl, pt.data(), pt.size());
        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> ciphertext(pending);
            BIO_read(wbio, ciphertext.data(), pending);

            // Fragment the Ciphertext, but pass the Plaintext size to the header!
            fragment_and_send_unlocked(channel, true, ciphertext, original_plaintext_size);
        }
    }
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
{
    // STRICT LOCKING: Hold both locks during encryption AND transmission
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    std::lock_guard<std::mutex> tx_lock(usb_tx_mutex);

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

            write_to_usb_unlocked(out);
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
            write_to_usb_unlocked(out);
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
