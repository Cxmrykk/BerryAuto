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

    write_to_usb(out);
}

void aap_send_raw(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();

    // The 4-byte unfragmented size must be included in the total payload length
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
    write_to_usb(out);
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t base_flags,
                                  uint32_t /*unused*/)
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
            std::vector<uint8_t> full_buffer(pending);
            BIO_read(wbio, full_buffer.data(), pending);

            size_t buf_offset = 0;
            // Iterate over the buffer and extract individual TLS records
            while (buf_offset + 5 <= full_buffer.size())
            {
                // Parse the TLS Record Header to find its exact length
                uint16_t record_len = (full_buffer[buf_offset + 3] << 8) | full_buffer[buf_offset + 4];
                uint32_t total_record_size = 5 + record_len;

                if (buf_offset + total_record_size > full_buffer.size())
                {
                    LOG_E("WARNING: Incomplete TLS record in wbio! Stream may desync.");
                    break;
                }

                std::vector<uint8_t> ciphertext(full_buffer.begin() + buf_offset,
                                                full_buffer.begin() + buf_offset + total_record_size);
                buf_offset += total_record_size;

                if (!is_tls_connected)
                {
                    uint16_t len_field = ciphertext.size() + 2;
                    std::vector<uint8_t> out;
                    out.push_back(0);
                    out.push_back(0x03);
                    out.push_back((len_field >> 8) & 0xFF);
                    out.push_back(len_field & 0xFF);
                    out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
                    out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
                    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
                    out_packets.push_back(out);
                }
                else
                {
                    size_t MAX_CHUNK_SIZE = 16000;
                    if (ciphertext.size() <= MAX_CHUNK_SIZE)
                    {
                        uint16_t len_field = ciphertext.size();
                        std::vector<uint8_t> out;
                        out.push_back(target_channel);
                        out.push_back(base_flags);
                        out.push_back((len_field >> 8) & 0xFF);
                        out.push_back(len_field & 0xFF);
                        out.insert(out.end(), ciphertext.begin(), ciphertext.end());
                        out_packets.push_back(out);
                    }
                    else
                    {
                        size_t offset = 0;
                        uint32_t total_size = ciphertext.size();

                        while (offset < total_size)
                        {
                            size_t remain = total_size - offset;
                            size_t chunk_size = std::min(remain, MAX_CHUNK_SIZE);

                            uint8_t flag;
                            if (offset == 0)
                                flag = (base_flags & ~0x03) | 0x01; // First Fragment
                            else if (offset + chunk_size >= total_size)
                                flag = (base_flags & ~0x03) | 0x02; // Last Fragment
                            else
                                flag = (base_flags & ~0x03) | 0x00; // Middle Fragment

                            std::vector<uint8_t> out;
                            out.push_back(target_channel);
                            out.push_back(flag);

                            uint16_t len_field = chunk_size;
                            if ((flag & 0x03) == 0x01)
                            {
                                len_field += 4;
                            }

                            out.push_back((len_field >> 8) & 0xFF);
                            out.push_back(len_field & 0xFF);

                            if ((flag & 0x03) == 0x01)
                            {
                                out.push_back((total_size >> 24) & 0xFF);
                                out.push_back((total_size >> 16) & 0xFF);
                                out.push_back((total_size >> 8) & 0xFF);
                                out.push_back(total_size & 0xFF);
                            }

                            out.insert(out.end(), ciphertext.begin() + offset,
                                       ciphertext.begin() + offset + chunk_size);
                            out_packets.push_back(out);

                            offset += chunk_size;
                        }
                    }
                }
            }
        }
    }

    for (const auto& pkt : out_packets)
    {
        write_to_usb(pkt);
    }
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    bool is_control = (type >= 1 && type <= 26);

    if (ssl_bypassed)
    {
        uint8_t flags = 0x03;
        if (channel != 0 && is_control)
            flags = 0x07;
        send_unencrypted(channel, flags, type, serialized);
    }
    else
    {
        uint8_t flags = 0x0B;
        if (channel != 0 && is_control)
            flags = 0x0F;

        std::vector<uint8_t> plaintext;
        plaintext.push_back((type >> 8) & 0xFF);
        plaintext.push_back(type & 0xFF);
        plaintext.insert(plaintext.end(), serialized.begin(), serialized.end());

        ssl_write_and_flush_unlocked(plaintext, channel, flags, 0);
    }
}
