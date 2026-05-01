#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);
void send_media_message(uint8_t channel, const std::vector<uint8_t>& payload);
void send_version_response(uint16_t major, uint16_t minor);
void flush_ssl_handshake();
