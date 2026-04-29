#include "video_sender.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <unistd.h>

void send_video_frame_internal(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    const size_t MAX_CHUNK_SIZE = 16384;

    std::vector<uint8_t> header;
    header.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_hi)
    header.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_lo)
    for (int i = 7; i >= 0; --i)
    {
        header.push_back((timestamp >> (i * 8)) & 0xFF);
    }

    uint32_t total_size = header.size() + nal_data.size();

    if (ssl_bypassed)
    {
        if (total_size <= MAX_CHUNK_SIZE)
        {
            std::vector<uint8_t> pt = header;
            pt.insert(pt.end(), nal_data.begin(), nal_data.end());
            aap_send_raw(pt, video_channel_id, 0x03, 0);
        }
        else
        {
            size_t data_in_first = MAX_CHUNK_SIZE - header.size();
            std::vector<uint8_t> pt = header;
            pt.insert(pt.end(), nal_data.begin(), nal_data.begin() + data_in_first);

            aap_send_raw(pt, video_channel_id, 0x01, total_size);

            size_t offset = data_in_first;
            while (offset < nal_data.size())
            {
                size_t remain = nal_data.size() - offset;
                size_t chunk_size = std::min(remain, MAX_CHUNK_SIZE);
                std::vector<uint8_t> pt_chunk(nal_data.begin() + offset, nal_data.begin() + offset + chunk_size);

                uint8_t flag = (offset + chunk_size >= nal_data.size()) ? 0x02 : 0x00;
                aap_send_raw(pt_chunk, video_channel_id, flag, 0);
                offset += chunk_size;
            }
        }
    }
    else
    {
        // Assemble full plaintext payload and let the SSL handler encrypt + fragment
        std::vector<uint8_t> pt = header;
        pt.insert(pt.end(), nal_data.begin(), nal_data.end());
        ssl_write_and_flush_unlocked(pt, video_channel_id, 0x0B, 0);
    }
    video_unacked_count++;
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    if (!(is_tls_connected || ssl_bypassed) || !video_channel_ready || !is_video_streaming.load())
    {
        return;
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
