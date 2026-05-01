#include "video_sender.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <unistd.h>

std::mutex config_mutex;
std::vector<uint8_t> cached_config_nal;
bool has_cached_config = false;

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    std::vector<uint8_t> pt;
    pt.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_hi)
    pt.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_lo)
    for (int i = 7; i >= 0; --i)
    {
        pt.push_back((timestamp >> (i * 8)) & 0xFF);
    }

    pt.insert(pt.end(), nal_data.begin(), nal_data.end());

    // CRITICAL: Raw Media payloads MUST NOT be encrypted!
    // We force the 0x00 base flag (Unencrypted Data) regardless of SSL status.
    aap_send_raw(pt, video_channel_id, 0x00, 0);

    video_unacked_count++;
}

void extract_and_cache_sps_pps(const std::vector<uint8_t>& frame)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    if (has_cached_config)
        return;

    std::vector<uint8_t> config_data;
    size_t i = 0;
    while (i < frame.size() - 3)
    {
        size_t start_code_len = 0;
        if (frame[i] == 0 && frame[i + 1] == 0 && frame[i + 2] == 1)
        {
            start_code_len = 3;
        }
        else if (frame[i] == 0 && frame[i + 1] == 0 && frame[i + 2] == 0 && frame[i + 3] == 1)
        {
            start_code_len = 4;
        }

        if (start_code_len > 0)
        {
            uint8_t header_byte = frame[i + start_code_len];
            uint8_t nal_type;
            bool is_config = false;

            if (global_video_codec_type == 7) // HEVC (H.265)
            {
                nal_type = (header_byte >> 1) & 0x3F;
                if (nal_type == 32 || nal_type == 33 || nal_type == 34)
                    is_config = true; // VPS, SPS, PPS
            }
            else // H.264
            {
                nal_type = header_byte & 0x1F;
                if (nal_type == 7 || nal_type == 8)
                    is_config = true; // SPS, PPS
            }

            if (is_config)
            {
                size_t next_nal = frame.size();
                for (size_t j = i + start_code_len; j < frame.size() - 2; j++)
                {
                    if ((frame[j] == 0 && frame[j + 1] == 0 && frame[j + 2] == 1) ||
                        (j < frame.size() - 3 && frame[j] == 0 && frame[j + 1] == 0 && frame[j + 2] == 0 &&
                         frame[j + 3] == 1))
                    {
                        next_nal = j;
                        break;
                    }
                }
                config_data.insert(config_data.end(), frame.begin() + i, frame.begin() + next_nal);
                i = next_nal;
            }
            else
            {
                break;
            }
        }
        else
        {
            i++;
        }
    }

    if (!config_data.empty())
    {
        cached_config_nal = config_data;
        has_cached_config = true;
        LOG_I(">>> Successfully extracted and cached Configuration! (" << config_data.size() << " bytes) <<<");
    }
}

void inject_cached_video_config()
{
    std::vector<uint8_t> config_copy;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!has_cached_config)
            return;
        config_copy = cached_config_nal;
    }

    std::vector<uint8_t> pt;
    pt.push_back(0x00); // msg_type_hi
    pt.push_back(0x01); // msg_type_lo = 1 (MEDIA_MESSAGE_CODEC_CONFIG)
    pt.insert(pt.end(), config_copy.begin(), config_copy.end());

    LOG_I(">>> Sending CODEC_CONFIG to Head Unit (" << config_copy.size() << " bytes)... <<<");

    // CRITICAL: Codec Configs are part of the Media Payload and MUST NOT be encrypted!
    aap_send_raw(pt, video_channel_id, 0x00, 0);
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    if (!(is_tls_connected || ssl_bypassed) || !video_channel_ready || !is_video_streaming.load())
    {
        return;
    }

    bool just_extracted = false;
    if (!has_cached_config)
    {
        extract_and_cache_sps_pps(nal_data);
        if (has_cached_config)
        {
            just_extracted = true;
        }
    }

    if (just_extracted)
    {
        inject_cached_video_config();
    }

    int wait_cycles = 0;
    while (is_video_streaming.load() && video_unacked_count.load() >= max_video_unacked && wait_cycles < 500)
    {
        std::this_thread::yield();
        usleep(2000);
        wait_cycles++;
    }

    if (!is_video_streaming.load())
        return;

    if (wait_cycles >= 500)
    {
        LOG_E("[WARNING] Video ACK timeout (1000ms). Stream bottlenecked, dropping frame to relieve pipeline...");
        if (video_streamer)
            video_streamer->force_keyframe();
        return;
    }

    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    send_video_frame(nal_data, timestamp);
}
