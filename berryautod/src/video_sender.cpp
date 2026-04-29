#include "video_sender.hpp"
#include "globals.hpp"
#include "aap_sender.hpp"
#include "video_encoder.hpp" 
#include <chrono>
#include <thread>
#include <unistd.h>
#include <algorithm>

std::mutex config_mutex;
std::vector<uint8_t> cached_config_nal;
bool has_cached_config = false;

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp) {
    const size_t MAX_CHUNK_SIZE = 16384;
    
    std::vector<uint8_t> header;
    header.push_back(0x00); // MEDIA_MESSAGE_DATA
    header.push_back(0x00);
    for(int i=7; i>=0; --i) { header.push_back((timestamp >> (i*8)) & 0xFF); }
    
    uint32_t total_unencrypted_size = header.size() + nal_data.size();
    
    if (total_unencrypted_size <= MAX_CHUNK_SIZE) {
        std::vector<uint8_t> pt = header;
        pt.insert(pt.end(), nal_data.begin(), nal_data.end());
        ssl_write_and_flush_unlocked(pt, video_channel_id, 0x0B, 0);
    } else {
        size_t data_in_first = MAX_CHUNK_SIZE - header.size();
        std::vector<uint8_t> pt = header;
        pt.insert(pt.end(), nal_data.begin(), nal_data.begin() + data_in_first);
        ssl_write_and_flush_unlocked(pt, video_channel_id, 0x09, total_unencrypted_size); 
        
        size_t offset = data_in_first;
        while (offset < nal_data.size()) {
            size_t remain = nal_data.size() - offset;
            size_t chunk_size = std::min(remain, MAX_CHUNK_SIZE);
            std::vector<uint8_t> pt_chunk(nal_data.begin() + offset, nal_data.begin() + offset + chunk_size);
            
            uint8_t flag = (offset + chunk_size >= nal_data.size()) ? 0x0A : 0x08; 
            ssl_write_and_flush_unlocked(pt_chunk, video_channel_id, flag, 0);
            offset += chunk_size;
        }
    }
    video_unacked_count++;
}

void extract_and_cache_sps_pps(const std::vector<uint8_t>& frame) {
    std::lock_guard<std::mutex> lock(config_mutex);
    if (has_cached_config) return;

    std::vector<uint8_t> config_data;
    size_t i = 0;
    while (i < frame.size() - 4) {
        if (frame[i] == 0 && frame[i+1] == 0 && ((frame[i+2] == 1) || (frame[i+2] == 0 && frame[i+3] == 1))) {
            size_t start_code_len = (frame[i+2] == 1) ? 3 : 4;
            uint8_t nal_type = frame[i + start_code_len] & 0x1F;
            
            size_t next_nal = frame.size();
            for (size_t j = i + start_code_len; j < frame.size() - 3; j++) {
                if (frame[j] == 0 && frame[j+1] == 0 && ((frame[j+2] == 1) || (frame[j+2] == 0 && frame[j+3] == 1))) {
                    next_nal = j;
                    break;
                }
            }
            
            if (nal_type == 7 || nal_type == 8) {
                config_data.insert(config_data.end(), frame.begin() + i, frame.begin() + next_nal);
            }
            i = next_nal;
        } else {
            i++;
        }
    }
    
    if (!config_data.empty()) {
        cached_config_nal = config_data;
        has_cached_config = true;
        LOG_I(">>> Successfully extracted and cached SPS/PPS Configuration! <<<");
    }
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp) {
    if (!is_tls_connected || !video_channel_ready || !is_video_streaming.load()) {
        return; 
    }

    if (!has_cached_config) {
        extract_and_cache_sps_pps(nal_data);
    }

    int wait_cycles = 0;
    while (is_video_streaming.load() && video_unacked_count.load() >= max_video_unacked && wait_cycles < 500) {
        std::this_thread::yield();
        usleep(2000); 
        wait_cycles++;
    }
    
    if (!is_video_streaming.load()) return;
    
    if (wait_cycles >= 500) {
        LOG_E("[WARNING] Video ACK timeout (1000ms). Stream bottlenecked, dropping frame to relieve pipeline...");
        if (video_streamer) video_streamer->force_keyframe();
        return; 
    }

    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp) {
    send_video_frame(nal_data, timestamp);
}

void inject_cached_video_config() {
    std::vector<uint8_t> config_copy;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!has_cached_config) return;
        config_copy = cached_config_nal;
    }
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    send_video_frame_internal(config_copy, ts);
}