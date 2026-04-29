#pragma once
#include <stdint.h>

void handle_channel_open_response();
void process_service_discovery_response(uint8_t* payload_data, int payload_len);
