#pragma once
#include <google/protobuf/message.h>
#include <stdint.h>
#include <vector>

void send_unencrypted(uint8_t channel, uint8_t flags, uint16_t type, const std::vector<uint8_t>& payload);
void aap_send_raw(const std::vector<uint8_t>& pt, uint8_t target_channel, uint8_t flags, uint32_t unfragmented_size);
void ssl_write_and_flush_unlocked(const std::vector<uint8_t>& pt, uint8_t target_channel = 0,
                                  uint8_t encrypted_flag = 0x0B, uint32_t unfragmented_size = 0);

// Smart wrapper that automatically routes to Encrypted or Unencrypted based on the ssl_bypassed flag
void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg);

// New bulk media handler that guarantees proper Encryption-before-Fragmentation order
void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt);
