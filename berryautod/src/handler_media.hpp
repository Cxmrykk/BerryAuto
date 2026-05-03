#pragma once
#include "globals.hpp"
#include <stdint.h>

void handle_media_message(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len, ChannelType ctype);
