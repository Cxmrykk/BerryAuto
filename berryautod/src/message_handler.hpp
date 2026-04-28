#pragma once
#include <stdint.h>

void handle_decrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len);
void handle_unencrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len);
