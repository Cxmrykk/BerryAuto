#pragma once
#include <stdint.h>

bool negotiate_wayland_screencast(uint32_t& out_node_id, int& out_fd);
void close_wayland_screencast();
