#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

// Clears pending packets from the queue upon disconnect
void flush_usb_tx_queue();

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload);
void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel = 0,
                                  uint8_t encrypted_flag = 0x0B, uint32_t unfragmented_size = 0);

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);

// Robust Media Handler. Returns true if queued, false if dropped due to backpressure.
bool send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt);
