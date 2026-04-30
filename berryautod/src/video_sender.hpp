#pragma once
#include <stdint.h>
#include <vector>

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp);
void extract_and_cache_sps_pps(const std::vector<uint8_t>& frame);
void inject_cached_video_config();
void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp);
void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp);
