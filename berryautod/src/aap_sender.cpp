#include "aap_sender.hpp"
#include "globals.hpp"
#include "control.pb.h"
#include <unistd.h>
#include <algorithm>
#include <mutex>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

std::mutex usb_tx_mutex;

void write_to_usb(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(usb_tx_mutex);
    const uint8_t* ptr = data.data();
    size_t remain = data.size();
    while (remain > 0) {
        int w = write(ep_in, ptr, remain);
        if (w < 0) {
            LOG_E("USB TX Write Failed!");
            break;
        }
        ptr += w;
        remain -= w;
    }
}

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload) {
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

void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t encrypted_flag, uint32_t unfragmented_size) {
    std::vector<std::vector<uint8_t>> out_packets;
    {
        std::lock_guard<std::recursive_mutex> lock(aap_mutex);
        
        // Push the payload into the SSL engine
        if (!pt.empty()) {
            SSL_write(ssl, pt.data(), pt.size());
        }

        // Immediately drain any produced TLS records while STILL holding aap_mutex
        // This ensures TLS records match the exact AAP channel/flags they were intended for
        while (true) {
            int pending = BIO_ctrl_pending(wbio);
            if (pending <= 0) break;

            int chunk_size = std::min(pending, 32768);
            std::vector<uint8_t> tls_record(chunk_size);
            BIO_read(wbio, tls_record.data(), chunk_size);
            
            if (!is_tls_connected) {
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
            } else {
                uint16_t len_field = tls_record.size();
                std::vector<uint8_t> out;
                out.push_back(target_channel);
                out.push_back(encrypted_flag);
                out.push_back((len_field >> 8) & 0xFF);
                out.push_back(len_field & 0xFF);
                
                if (encrypted_flag == 0x09) {
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
    for (const auto& pkt : out_packets) {
        write_to_usb(pkt);
    }
}

void send_encrypted(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg) {
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    std::vector<uint8_t> plaintext;
    plaintext.push_back((type >> 8) & 0xFF);
    plaintext.push_back(type & 0xFF);
    plaintext.insert(plaintext.end(), serialized.begin(), serialized.end());

    ssl_write_and_flush_unlocked(plaintext, channel, 0x0B, 0);
}