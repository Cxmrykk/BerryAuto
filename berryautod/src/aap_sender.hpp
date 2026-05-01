#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void flush_usb_tx_queue();
void flush_ssl_buffers();
int get_tx_queue_size();

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload);
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);
void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt);
