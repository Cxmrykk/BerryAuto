#pragma once
#include <stdint.h>
#include <vector>

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp, bool is_keyframe);
void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp, bool is_keyframe);
