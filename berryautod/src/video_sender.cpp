#include "video_sender.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <unistd.h>

static bool is_recovering = false;

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    std::vector<uint8_t> pt;
    pt.push_back(0x00);
    pt.push_back(0x00); // MEDIA_MESSAGE_DATA (Type 0)

    // 64-bit Timestamp (Big Endian) required for all non-Protobuf Media Data
    for (int i = 7; i >= 0; --i)
        pt.push_back((timestamp >> (i * 8)) & 0xFF);

    pt.insert(pt.end(), nal_data.begin(), nal_data.end());

    if (!send_media_payload(video_channel_id, pt))
    {
        LOG_E("[WARNING] Frame rejected by TX Queue.");
    }
    else
    {
        // Must increment to keep proper sync with the car's ACKs
        video_unacked_count++;
    }
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    if (!(is_tls_connected || ssl_bypassed) || !video_channel_ready || !is_video_streaming.load())
    {
        is_recovering = false;
        return;
    }

    if (is_recovering)
    {
        // Wait until BOTH the USB pipeline has drained AND the car's decoder has caught up with ACKs
        if (get_tx_queue_size() > 0 || (max_video_unacked > 0 && video_unacked_count.load() >= max_video_unacked))
            return;

        if (video_streamer)
            video_streamer->force_keyframe(); // Ensures the next frame has fresh inline SPS/PPS

        is_recovering = false;
        LOG_I("[RECOVERY] USB Pipeline is clear and decoder caught up. Requesting Keyframe to resume stream.");
        return;
    }

    // Protocol flow control: Prevent overwhelming the car's hardware decoder
    if (max_video_unacked > 0 && video_unacked_count.load() >= max_video_unacked)
    {
        LOG_E("[WARNING] Car decoder lagging! Max unacked reached (" + std::to_string(video_unacked_count.load()) +
              "). Dropping frame.");
        is_recovering = true;
        return;
    }

    // 30 frames in the queue = exactly 1 second of lag. Dump it to catch up.
    if (get_tx_queue_size() >= 30)
    {
        LOG_E("[WARNING] USB Queue Congested! Dropping frames to catch up.");
        flush_usb_tx_queue();
        is_recovering = true;
        return;
    }

    // Pass the raw, pristine H.264 frame straight from the hardware encoder directly to the car.
    // The hardware encoder outputs valid Annex-B framing natively.
    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    send_video_frame(nal_data, timestamp);
}
