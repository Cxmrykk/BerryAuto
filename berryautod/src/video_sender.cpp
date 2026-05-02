#include "video_sender.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    std::vector<uint8_t> pt;
    pt.push_back(0x00);
    pt.push_back(0x00); // MEDIA_MESSAGE_DATA (Type 0)

    // 64-bit Timestamp (Big Endian)
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
    if (!(is_tls_connected || ssl_bypassed) || !video_channel_ready || !is_video_streaming.load())
        return;

    // FIX: Flow Control relies strictly on the USB queue size.
    // If the pipeline fills beyond 15 frames (0.5s of latency), it means the car's
    // hardware decoder buffer is saturated. Clear our queue and force an IDR refresh.
    if (get_tx_queue_size() >= 15)
    {
        LOG_E("[WARNING] USB Queue Congested! Flushing pipeline and forcing a Keyframe.");
        flush_usb_tx_queue();
        if (video_streamer)
            video_streamer->force_keyframe();
        return; // Drop this frame, let the stream catch up.
    }

    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    send_video_frame(nal_data, timestamp);
}
