#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void flush_usb_tx_queue();
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);

// Media handler returns false if the queue is overloaded, allowing pre-emptive frame drops
bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt);
