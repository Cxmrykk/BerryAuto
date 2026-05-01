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

void write_fragmented_packet(const std::vector<uint8_t>& payload, uint8_t target_channel, bool is_encrypted,
                             bool is_control)
{
    uint8_t base_flag = 0;
    if (is_encrypted)
        base_flag |= 0x08;

    // Channel 0 is implicitly a control channel, so the 0x04 flag is only needed for service channels
    if (is_control && target_channel != 0)
        base_flag |= 0x04;

    const size_t MAX_CHUNK_SIZE = 16000;

    if (payload.size() <= MAX_CHUNK_SIZE)
    {
        uint8_t flag = base_flag | 0x03; // Unfragmented
        uint16_t len_field = payload.size();
        std::vector<uint8_t> out;
        out.push_back(target_channel);
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
            size_t chunk_size = std::min(remain, MAX_CHUNK_SIZE);
            bool is_first = (offset == 0);
            bool is_last = (offset + chunk_size >= total_size);

            uint8_t flag = base_flag;
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
            chunk.push_back(target_channel);
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

        // Write all fragments at once atomically to prevent interleaving
        write_to_usb(out_payload);
    }
}

void ssl_write_and_flush_handshake()
{
    std::vector<uint8_t> ciphertext;
    {
        std::lock_guard<std::recursive_mutex> lock(aap_mutex);
        int pending = BIO_ctrl_pending(wbio);
        while (pending > 0)
        {
            std::vector<uint8_t> temp(pending);
            BIO_read(wbio, temp.data(), pending);
            ciphertext.insert(ciphertext.end(), temp.begin(), temp.end());
            pending = BIO_ctrl_pending(wbio);
        }
    }

    if (!ciphertext.empty())
    {
        std::vector<uint8_t> pt;
        pt.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
        pt.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
        pt.insert(pt.end(), ciphertext.begin(), ciphertext.end());
        write_fragmented_packet(pt, 0, false, false);
    }
}

void send_encrypted_message(uint8_t channel, bool is_control, const std::vector<uint8_t>& pt)
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
            std::vector<uint8_t> temp(pending);
            BIO_read(wbio, temp.data(), pending);
            ciphertext.insert(ciphertext.end(), temp.begin(), temp.end());
            pending = BIO_ctrl_pending(wbio);
        }
    }
    if (!ciphertext.empty())
    {
        write_fragmented_packet(ciphertext, channel, true, is_control);
    }
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    std::cout << "[DEBUG] SEND Channel: " << (int)channel << " Type: " << type << " Size: " << serialized.size()
              << std::endl;

    // FIX 2: All Protobuf messages are Control Messages.
    bool is_control = true;

    std::vector<uint8_t> pt;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), serialized.begin(), serialized.end());

    if (ssl_bypassed)
    {
        write_fragmented_packet(pt, channel, false, is_control);
    }
    else
    {
        send_encrypted_message(channel, is_control, pt);
    }
}

void send_media_message(uint8_t channel, const std::vector<uint8_t>& payload)
{
    // FIX 1: Media Payloads (Video/Audio/Codec Configs) are Data Messages, and must NEVER be encrypted.
    write_fragmented_packet(payload, channel, false, false);
}

void send_version_response(uint16_t major, uint16_t minor)
{
    std::vector<uint8_t> pt;
    uint16_t type = ControlMsgType::MESSAGE_VERSION_RESPONSE;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.push_back((major >> 8) & 0xFF);
    pt.push_back(major & 0xFF);
    pt.push_back((minor >> 8) & 0xFF);
    pt.push_back(minor & 0xFF);
    pt.push_back(0x00);
    pt.push_back(0x00);

    write_fragmented_packet(pt, 0, false, false);
}
