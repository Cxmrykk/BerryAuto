#include "video_sender.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include <chrono>
#include <mutex>

std::mutex config_mutex;
std::vector<uint8_t> cached_config_nal;
bool has_cached_config = false;
bool config_injected_this_session = false;

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
            start_code_len = 3;
        else if (frame[i] == 0 && frame[i + 1] == 0 && frame[i + 2] == 0 && frame[i + 3] == 1)
            start_code_len = 4;

        if (start_code_len > 0)
        {
            uint8_t header_byte = frame[i + start_code_len];
            uint8_t nal_type = (global_video_codec_type == 7) ? ((header_byte >> 1) & 0x3F) : (header_byte & 0x1F);
            bool is_config =
                (global_video_codec_type == 7) ? (nal_type >= 32 && nal_type <= 34) : (nal_type == 7 || nal_type == 8);

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
                break;
        }
        else
            i++;
    }

    if (!config_data.empty())
    {
        cached_config_nal = config_data;
        has_cached_config = true;
        LOG_I(">>> Cached Video Configuration! (" << config_data.size() << " bytes) <<<");
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
    pt.push_back(0x00);
    pt.push_back(0x01); // MEDIA_MESSAGE_CODEC_CONFIG (Type 1)
    pt.insert(pt.end(), config_copy.begin(), config_copy.end());
    send_media_payload(video_channel_id, pt);
}

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    std::vector<uint8_t> pt;
    pt.push_back(0x00);
    pt.push_back(0x00); // MEDIA_MESSAGE_DATA (Type 0)

    // AAP Timestamps must be 64-bit Big-Endian and perfectly match the encoded stream
    for (int i = 7; i >= 0; --i)
        pt.push_back((timestamp >> (i * 8)) & 0xFF);

    pt.insert(pt.end(), nal_data.begin(), nal_data.end());

    if (!send_media_payload(video_channel_id, pt))
    {
        LOG_E("[WARNING] Frame rejected by TX Queue.");
    }
    else
    {
        video_unacked_count++;
    }
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    static bool was_streaming = false;
    bool currently_streaming = is_video_streaming.load() && video_channel_ready && (is_tls_connected || ssl_bypassed);

    // Edge Detector: Detects when the stream resumes after stopping/exiting
    if (currently_streaming && !was_streaming)
    {
        config_injected_this_session = false;
        video_unacked_count = 0; // Reset ACK flow control for the new session
    }
    was_streaming = currently_streaming;

    if (!currently_streaming)
        return;

    // 1. Session Init: Must send CODEC_CONFIG exactly once per session
    if (!config_injected_this_session)
    {
        if (!has_cached_config)
        {
            extract_and_cache_sps_pps(nal_data);
        }

        if (has_cached_config)
        {
            inject_cached_video_config();
            config_injected_this_session = true;
        }
        else
        {
            // Cannot send video data until SPS/PPS is processed
            if (video_streamer)
                video_streamer->force_keyframe();
            return;
        }
    }

    // 2. Strict Protocol Flow Control
    if (max_video_unacked > 0 && video_unacked_count.load() >= max_video_unacked)
    {
        // The car's decoder is still digesting frames. Wait for ACKs.
        // We drop this frame quietly to maintain stability and ask for a fresh keyframe.
        if (video_streamer)
            video_streamer->force_keyframe();
        return;
    }

    // 3. Relaxed USB Backpressure Failsafe for 2K/4K @ 60 FPS overhead
    if (get_tx_queue_size() >= 60)
    {
        LOG_E("[WARNING] USB Queue Congested! Flushing pipeline and forcing a Keyframe.");
        flush_usb_tx_queue();
        if (video_streamer)
            video_streamer->force_keyframe();
        return;
    }

    // 4. Send Frame
    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    send_video_frame(nal_data, timestamp);
}
