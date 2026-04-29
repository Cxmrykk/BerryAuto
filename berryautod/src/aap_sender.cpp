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

    std::cout << "[DEBUG-TX] Unencrypted - Channel: " << (int)channel << " Type: " << type << " Size: " << out.size()
              << std::endl;
    write_to_usb(out);
}

void aap_send_raw(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size)
{
    std::vector<uint8_t> out;
    uint16_t len_field = pt.size();
    out.push_back(target_channel);
    out.push_back(flags);
    out.push_back((len_field >> 8) & 0xFF);
    out.push_back(len_field & 0xFF);

    if (flags == 0x09)
    { // First Fragment has 4-byte unfragmented size
        out.push_back((unfragmented_size >> 24) & 0xFF);
        out.push_back((unfragmented_size >> 16) & 0xFF);
        out.push_back((unfragmented_size >> 8) & 0xFF);
        out.push_back(unfragmented_size & 0xFF);
    }

    out.insert(out.end(), pt.begin(), pt.end());
    write_to_usb(out);
}

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag,
                                  uint32_t unfragmented_size)
{
    std::vector<std::vector<uint8_t>> out_packets;
    {
        std::lock_guard<std::recursive_mutex> lock(aap_mutex);

        // Push the payload into the SSL engine
        if (!pt.empty())
        {
            SSL_write(ssl, pt.data(), pt.size());
        }

        // Immediately drain any produced TLS records while STILL holding aap_mutex
        while (true)
        {
            int pending = BIO_ctrl_pending(wbio);
            if (pending <= 0)
                break;

            int chunk_size = std::min(pending, 32768);
            std::vector<uint8_t> tls_record(chunk_size);
            BIO_read(wbio, tls_record.data(), chunk_size);

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
                out.push_back(target_channel);
                out.push_back(encrypted_flag);
                out.push_back((len_field >> 8) & 0xFF);
                out.push_back(len_field & 0xFF);

                if (encrypted_flag == 0x09)
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

    // Write out the packets using blocking I/O *WITHOUT* holding the vital AAP mutex
    for (const auto& pkt : out_packets)
    {
        // Only hide logs for actual streaming video payload frames (0x08, 0x09, 0x0A)
        if (!(target_channel == 2 && (encrypted_flag == 0x08 || encrypted_flag == 0x09 || encrypted_flag == 0x0A)))
        {
            std::cout << "[DEBUG-TX] Encrypted - Channel: " << (int)target_channel << " Flags: 0x" << std::hex
                      << (int)encrypted_flag << std::dec << " Size: " << pkt.size() << std::endl;
        }
        write_to_usb(pkt);
    }
}

// Master wrapper to automatically handle Car TLS Bypasses
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    std::cout << "[DEBUG] SEND Channel: " << (int)channel << " Type: " << type << " Size: " << serialized.size()
              << std::endl;

    // FIX: Only types 1-26 are generic control messages requiring the 0x04 control bit on non-zero channels.
    // Media and Sensor setup messages (>32768) are channel-specific and MUST NOT have the control flag.
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