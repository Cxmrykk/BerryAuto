#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void write_to_usb(const std::vector<uint8_t>& data);
void write_fragmented_packet(const std::vector<uint8_t>& payload, uint8_t target_channel, bool is_encrypted,
                             bool is_control);
void ssl_write_and_flush_handshake();
void send_encrypted_message(uint8_t channel, bool is_control, const std::vector<uint8_t>& pt);

// Smart wrappers that automatically route to Encrypted or Unencrypted based on the ssl_bypassed flag
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);
void send_media_message(uint8_t channel, const std::vector<uint8_t>& payload);
void send_version_response(uint16_t major, uint16_t minor);
