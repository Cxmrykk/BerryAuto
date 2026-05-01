#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <iostream>
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

// The master framing function that perfectly mirrors the official Android Auto spec
void send_aap_message(uint8_t channel, bool is_encrypted, bool is_control, const std::vector<uint8_t>& plaintext)
{
    uint8_t base_flag = 0;
    if (is_encrypted)
        base_flag |= 0x08;

    // Channel 0 is implicitly a control channel, so the 0x04 flag is only required for service channels
    if (is_control && channel != 0)
        base_flag |= 0x04;

    const size_t MAX_PAYLOAD_SIZE = 16000;
    std::vector<uint8_t> final_usb_payload;

    if (plaintext.size() <= MAX_PAYLOAD_SIZE)
    {
        uint8_t flag = base_flag | 0x03; // 0x03 = Unfragmented

        std::vector<uint8_t> payload_to_send;
        if (is_encrypted)
        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            SSL_write(ssl, plaintext.data(), plaintext.size());
            int pending = BIO_ctrl_pending(wbio);
            if (pending > 0)
            {
                payload_to_send.resize(pending);
                BIO_read(wbio, payload_to_send.data(), pending);
            }
        }
        else
        {
            payload_to_send = plaintext;
        }

        uint16_t len_field = payload_to_send.size();
        final_usb_payload.push_back(channel);
        final_usb_payload.push_back(flag);
        final_usb_payload.push_back((len_field >> 8) & 0xFF);
        final_usb_payload.push_back(len_field & 0xFF);
        final_usb_payload.insert(final_usb_payload.end(), payload_to_send.begin(), payload_to_send.end());
    }
    else
    {
        uint32_t total_size = plaintext.size();
        size_t offset = 0;

        while (offset < total_size)
        {
            bool is_first = (offset == 0);
            size_t max_chunk = MAX_PAYLOAD_SIZE;

            // Leave 4 bytes of room for the total_size header in the first fragment
            if (is_first)
                max_chunk -= 4;

            size_t chunk_size = std::min(total_size - offset, max_chunk);
            bool is_last = (offset + chunk_size >= total_size);

            uint8_t flag = base_flag;
            if (is_first)
                flag |= 0x01;
            else if (is_last)
                flag |= 0x02;
            else
                flag |= 0x00;

            // 1. Build the plaintext chunk
            std::vector<uint8_t> frag_plaintext;
            if (is_first)
            {
                // The Total Size goes inside the encrypted payload of the FIRST fragment ONLY
                frag_plaintext.push_back((total_size >> 24) & 0xFF);
                frag_plaintext.push_back((total_size >> 16) & 0xFF);
                frag_plaintext.push_back((total_size >> 8) & 0xFF);
                frag_plaintext.push_back(total_size & 0xFF);
            }
            frag_plaintext.insert(frag_plaintext.end(), plaintext.begin() + offset,
                                  plaintext.begin() + offset + chunk_size);

            // 2. Encrypt the individual chunk (creates an isolated TLS record)
            std::vector<uint8_t> payload_to_send;
            if (is_encrypted)
            {
                std::lock_guard<std::recursive_mutex> lock(aap_mutex);
                SSL_write(ssl, frag_plaintext.data(), frag_plaintext.size());
                int pending = BIO_ctrl_pending(wbio);
                if (pending > 0)
                {
                    payload_to_send.resize(pending);
                    BIO_read(wbio, payload_to_send.data(), pending);
                }
            }
            else
            {
                payload_to_send = frag_plaintext;
            }

            // 3. Wrap the TLS record in an AAP Header
            uint16_t len_field = payload_to_send.size();
            final_usb_payload.push_back(channel);
            final_usb_payload.push_back(flag);
            final_usb_payload.push_back((len_field >> 8) & 0xFF);
            final_usb_payload.push_back(len_field & 0xFF);
            final_usb_payload.insert(final_usb_payload.end(), payload_to_send.begin(), payload_to_send.end());

            offset += chunk_size;
        }
    }

    // 4. Atomically write the ENTIRE sequence of fragments to USB to prevent interleaving
    write_to_usb(final_usb_payload);
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

        // Handshake TLS records are sent unencrypted over AAP Channel 0
        send_aap_message(0, false, false, pt);
    }
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    // In AAP, all Protocol Buffer messages are considered "Control Messages" (Type < 0x8000)
    bool is_control = (type < 0x8000);

    std::vector<uint8_t> pt;
    pt.push_back((type >> 8) & 0xFF);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), serialized.begin(), serialized.end());

    send_aap_message(channel, !ssl_bypassed, is_control, pt);
}

void send_media_message(uint8_t channel, const std::vector<uint8_t>& payload)
{
    // Media Payloads (Video/Audio/Config) are "Data Messages" (Not Control) and are encrypted if TLS is active.
    send_aap_message(channel, !ssl_bypassed, false, payload);
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

    send_aap_message(0, false, false, pt);
}
