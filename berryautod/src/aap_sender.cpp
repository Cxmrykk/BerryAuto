#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <mutex>
#include <unistd.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

std::mutex usb_tx_mutex;

void write_to_usb(const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(usb_tx_mutex);
    const uint8_t* ptr = data.data();
    size_t remain = data.size();
    while (remain > 0)
    {
        int w = write(ep_in, ptr, remain);
        if (w < 0)
        {
            LOG_E("USB TX Write Failed!");
            break;
        }
        ptr += w;
        remain -= w;
    }
}

// Internal helper to correctly fragment payloads (plaintext or ciphertext)
void fragment_and_send(const std::vector<uint8_t>& payload, uint8_t channel, uint8_t base_flags)
{
    const size_t MAX_CHUNK_SIZE = 16000;

    if (payload.size() <= MAX_CHUNK_SIZE)
    {
        uint8_t flag = base_flags | 0x03; // Unfragmented
        uint16_t len_field = payload.size();
        std::vector<uint8_t> out;
        out.push_back(channel);
        out.push_back(flag);
        out.push_back((len_field >> 8) & 0xFF);
        out.push_back(len_field & 0xFF);
        out.insert(out.end(), payload.begin(), payload.end());
        write_to_usb(out);
    }
    else
    {
        std::vector<uint8_t> out_payload;
        uint32_t total_size = payload.size();
        size_t offset = 0;

        while (offset < total_size)
        {
            size_t remain = total_size - offset;
            bool is_first = (offset == 0);
            size_t max_payload = MAX_CHUNK_SIZE;
            if (is_first)
                max_payload -= 4; // leave room for unfragmented size

            size_t chunk_size = std::min(remain, max_payload);
            bool is_last = (offset + chunk_size >= total_size);

            uint8_t flag = base_flags;
            if (is_first)
                flag |= 0x01;
            else if (is_last)
                flag |= 0x02;
            else
                flag |= 0x00;

            uint16_t len_field = chunk_size;
            if (is_first)
                len_field += 4;

            std::vector<uint8_t> chunk;
            chunk.push_back(channel);
            chunk.push_back(flag);
            chunk.push_back((len_field >> 8) & 0xFF);
            chunk.push_back(len_field & 0xFF);

            if (is_first)
            {
                chunk.push_back((total_size >> 24) & 0xFF);
                chunk.push_back((total_size >> 16) & 0xFF);
                chunk.push_back((total_size >> 8) & 0xFF);
                chunk.push_back(total_size & 0xFF);
            }

            chunk.insert(chunk.end(), payload.begin() + offset, payload.begin() + offset + chunk_size);
            out_payload.insert(out_payload.end(), chunk.begin(), chunk.end());

            offset += chunk_size;
        }
        // Send everything atomically
        write_to_usb(out_payload);
    }
}

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> pt;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), payload.begin(), payload.end());

    uint8_t base_flags = flags & 0xFC; // Strip fragmentation bits
    fragment_and_send(pt, channel, base_flags);
}

void aap_send_raw(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    uint8_t base_flags = flags & 0xFC;
    fragment_and_send(pt, target_channel, base_flags);
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
{
    std::vector<uint8_t> ciphertext;
    {
        std::lock_guard<std::recursive_mutex> lock(aap_mutex);

        if (!pt.empty())
        {
            SSL_write(ssl, pt.data(), pt.size());
        }

        int pending = BIO_ctrl_pending(wbio);
        while (pending > 0)
        {
            std::vector<uint8_t> tls_record(pending);
            BIO_read(wbio, tls_record.data(), pending);
            ciphertext.insert(ciphertext.end(), tls_record.begin(), tls_record.end());
            pending = BIO_ctrl_pending(wbio);
        }
    }

    if (ciphertext.empty())
        return;

    if (!is_tls_connected)
    {
        std::vector<uint8_t> out;
        out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
        out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
        out.insert(out.end(), ciphertext.begin(), ciphertext.end());
        fragment_and_send(out, 0, 0x00);
    }
    else
    {
        uint8_t base_flags = encrypted_flag & 0xFC;
        fragment_and_send(ciphertext, target_channel, base_flags);
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
