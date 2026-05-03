#pragma once
#include <stdint.h>

void handle_control_message(uint16_t type, uint8_t* payload_data, int payload_len);
