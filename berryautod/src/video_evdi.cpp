#include "globals.hpp"
#include "video_encoder.hpp"
#include <cstring>
#include <iostream>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

extern uint64_t get_monotonic_usec();

// Pre-computed, VESA-compliant EDID blocks to prevent compositor fallbacks
std::vector<uint8_t> VideoEncoder::get_edid(int width, int height)
{
    if (width >= 1920)
    {
        // 1920x1080 @ 60Hz
        return {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
                0x45, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x43,
                0x45, 0x0F, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24};
    }
    else if (width >= 1280)
    {
        // 1280x720 @ 60Hz
        return {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28,
                0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x43,
                0x45, 0x08, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C};
    }
    else
    {
        // 800x480 @ 60Hz
        return {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x96, 0x0B, 0x20, 0x8A, 0x30, 0xE0, 0x1B, 0x10, 0x28, 0x30,
                0x33, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x1D,
                0x20, 0x03, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4};
    }
}

static void evdi_dpms_handler(int dpms_mode, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    LOG_I("[EVDI] DPMS State Changed: " << dpms_mode);
    if (dpms_mode == 0)
    {
        for (int i = 0; i < enc->evdi_buffer_count; ++i)
        {
            evdi_request_update(enc->evdi, i);
        }
    }
}

static void evdi_mode_changed_handler(evdi_mode mode, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    LOG_I("[EVDI] Compositor updated Mode: " << mode.width << "x" << mode.height);

    std::lock_guard<std::mutex> lock(enc->frame_mutex);
    enc->input_w = mode.width;
    enc->input_h = mode.height;
    enc->input_fmt = AV_PIX_FMT_BGRA;
    enc->update_sws();

    for (int i = 0; i < enc->evdi_buffer_count; ++i)
    {
        if (enc->evdi_buffers[i].buffer)
        {
            free(enc->evdi_buffers[i].buffer);
        }
        enc->evdi_buffers[i].id = i;
        enc->evdi_buffers[i].width = mode.width;
        enc->evdi_buffers[i].height = mode.height;
        enc->evdi_buffers[i].stride = mode.width * 4;
        enc->evdi_buffers[i].buffer = malloc(enc->evdi_buffers[i].stride * mode.height);
        evdi_register_buffer(enc->evdi, enc->evdi_buffers[i]);
    }
}

static void evdi_update_ready_handler(int buffer_to_be_updated, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);

    if (buffer_to_be_updated >= 0 && buffer_to_be_updated < enc->evdi_buffer_count)
    {
        evdi_buffer& buf = enc->evdi_buffers[buffer_to_be_updated];

        evdi_rect rects[16];
        int num_rects = 0;
        evdi_grab_pixels(enc->evdi, rects, &num_rects);

        // Push the new pixels into our thread-safe buffer
        if (buf.buffer && enc->input_w > 0 && enc->input_h > 0)
        {
            std::lock_guard<std::mutex> lock(enc->frame_mutex);
            size_t req_size = buf.stride * buf.height;
            if (enc->latest_frame_buffer.size() != req_size)
            {
                enc->latest_frame_buffer.resize(req_size);
            }
            memcpy(enc->latest_frame_buffer.data(), buf.buffer, req_size);
            enc->latest_stride = buf.stride;
        }

        // Return the buffer to EVDI to get the next frame
        evdi_request_update(enc->evdi, buffer_to_be_updated);
    }
}

static void evdi_crtc_state_handler(int state, void* user_data)
{
    (void)user_data;
    LOG_I("[EVDI] CRTC State Changed: " << state);
}

void VideoEncoder::handle_evdi_update(int buffer_id)
{
    (void)buffer_id;
}

void VideoEncoder::run_evdi_loop()
{
    int device_num = -1;
    for (int i = 0; i < 10; ++i)
    {
        if (evdi_check_device(i) == AVAILABLE)
        {
            device_num = i;
            break;
        }
    }

    if (device_num == -1)
    {
        LOG_E("[EVDI] CRITICAL: No available EVDI devices found!");
        return;
    }

    evdi = evdi_open(device_num);
    if (evdi == EVDI_INVALID_HANDLE)
    {
        LOG_E("[EVDI] Failed to open device /dev/dri/card" << device_num);
        return;
    }

    memset(&evdi_ctx, 0, sizeof(evdi_ctx));
    evdi_ctx.dpms_handler = evdi_dpms_handler;
    evdi_ctx.mode_changed_handler = evdi_mode_changed_handler;
    evdi_ctx.update_ready_handler = evdi_update_ready_handler;
    evdi_ctx.crtc_state_handler = evdi_crtc_state_handler;
    evdi_ctx.user_data = this;

    evdi_buffers = new evdi_buffer[evdi_buffer_count];
    for (int i = 0; i < evdi_buffer_count; ++i)
    {
        evdi_buffers[i].buffer = nullptr;
    }

    std::vector<uint8_t> edid = get_edid(target_width, target_height);
    evdi_connect(evdi, edid.data(), edid.size(), 0);
    LOG_I("[EVDI] Virtual Monitor connected: " << target_width << "x" << target_height);

    // Launch a background thread dedicated solely to handling EVDI asynchronous events
    std::thread evdi_bg_thread(
        [this]()
        {
            int evdi_fd = evdi_get_event_ready(evdi);
            fd_set rfds;
            struct timeval tv;

            while (running.load())
            {
                FD_ZERO(&rfds);
                FD_SET(evdi_fd, &rfds);
                tv.tv_sec = 1;
                tv.tv_usec = 0;

                int retval = select(evdi_fd + 1, &rfds, NULL, NULL, &tv);
                if (retval > 0)
                {
                    evdi_handle_events(evdi, &evdi_ctx);
                }
            }
        });

    // Main foreground loop: Feed FFmpeg at exactly `target_fps` (e.g. 60 FPS)
    uint64_t frame_interval_us = 1000000 / target_fps;
    uint64_t next_frame_time = get_monotonic_usec() + frame_interval_us;

    while (running.load())
    {
        std::vector<uint8_t> frame_copy;
        int current_stride = 0, current_w = 0, current_h = 0;

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            current_w = input_w;
            current_h = input_h;
            current_stride = latest_stride;
            frame_copy = latest_frame_buffer;
        }

        // If the desktop hasn't drawn anything yet, we can't encode a frame
        if (!frame_copy.empty() && current_w > 0 && current_h > 0)
        {
            process_raw_frame(frame_copy.data(), current_stride, current_w, current_h);
        }

        uint64_t now = get_monotonic_usec();
        if (now < next_frame_time)
        {
            usleep(next_frame_time - now);
            next_frame_time += frame_interval_us;
        }
        else
        {
            next_frame_time = now + frame_interval_us;
        }
    }

    if (evdi_bg_thread.joinable())
    {
        evdi_bg_thread.join();
    }

    evdi_disconnect(evdi);

    for (int i = 0; i < evdi_buffer_count; ++i)
    {
        if (evdi_buffers[i].buffer)
        {
            evdi_unregister_buffer(evdi, evdi_buffers[i].id);
            free(evdi_buffers[i].buffer);
        }
    }
    delete[] evdi_buffers;

    evdi_close(evdi);
    LOG_I("[EVDI] Virtual Monitor destroyed.");
}
