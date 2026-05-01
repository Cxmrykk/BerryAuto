#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void write_to_usb(const std::vector<uint8_t>& data);
void send_aap_message(uint8_t channel, bool is_encrypted, bool is_control, const std::vector<uint8_t>& plaintext);
void ssl_write_and_flush_handshake();

// Smart wrappers that automatically route to Encrypted or Unencrypted based on the ssl_bypassed flag
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);
void send_media_message(uint8_t channel, const std::vector<uint8_t>& payload);
void send_version_response(uint16_t major, uint16_t minor);
