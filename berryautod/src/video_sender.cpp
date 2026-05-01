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
    // SLICING MAGIC: We limit every payload to 14,000 bytes.
    // This entirely circumvents AAP Message Fragmentation, ensuring every chunk
    // fits into a single TLS Record and cannot cause -251 Buffer Overflows!
    const size_t MAX_SLICE_SIZE = 14000;
    size_t offset = 0;

    while (offset < nal_data.size())
    {
        size_t remain = nal_data.size() - offset;
        size_t chunk_size = std::min(remain, MAX_SLICE_SIZE);

        std::vector<uint8_t> pt;
        pt.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_hi)
        pt.push_back(0x00); // MEDIA_MESSAGE_DATA (msg_type_lo)

        // Android Auto decoders reconstruct NAL units by grouping payloads with identical timestamps!
        for (int i = 7; i >= 0; --i)
        {
            pt.push_back((timestamp >> (i * 8)) & 0xFF);
        }

        pt.insert(pt.end(), nal_data.begin() + offset, nal_data.begin() + offset + chunk_size);

        send_media_payload(video_channel_id, pt);
        offset += chunk_size;

        // VITAL YIELD: This brief microsecond pause allows the USB RX thread
        // to grab the queue mutex and slip a Ping Response into the stream!
        std::this_thread::yield();
    }

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
    pt.push_back(0x01);
    pt.insert(pt.end(), config_copy.begin(), config_copy.end());
    send_media_payload(video_channel_id, pt);
}

void send_video_frame(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    if (!(is_tls_connected || ssl_bypassed) || !video_channel_ready || !is_video_streaming.load())
        return;

    // PRE-EMPTIVE DROP: If the USB pipeline is backed up by > 10 chunks (~150KB),
    // drop this video frame natively to prevent the pipeline from choking!
    if (get_tx_queue_size() > 10)
    {
        LOG_E("[WARNING] USB TX Queue backed up. Dropping frame to preserve latency.");
        return;
    }

    if (!has_cached_config)
    {
        extract_and_cache_sps_pps(nal_data);
        if (has_cached_config)
            inject_cached_video_config();
    }

    int wait_cycles = 0;
    while (is_video_streaming.load() && video_unacked_count.load() >= max_video_unacked && wait_cycles < 250)
    {
        std::this_thread::yield();
        usleep(2000);
        wait_cycles++;
    }

    if (!is_video_streaming.load())
        return;

    if (wait_cycles >= 250)
    {
        LOG_E("[WARNING] Video ACK timeout (500ms). Dropping frame.");
        return;
    }

    send_video_frame_internal(nal_data, timestamp);
}

void on_video_nal_ready(const std::vector<uint8_t>& nal_data, uint64_t timestamp)
{
    send_video_frame(nal_data, timestamp);
}
