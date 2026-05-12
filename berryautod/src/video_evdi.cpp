#include "globals.hpp"
#include "video_encoder.hpp"
#include <cstring>
#include <iostream>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

extern uint64_t get_monotonic_usec();

std::vector<uint8_t> VideoEncoder::get_edid(int width, int height)
{
    (void)height; // Suppress unused parameter warning

    std::vector<uint8_t> edid;

    if (width >= 1920)
    {
        edid = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
                0x45, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x43,
                0x45, 0x0F, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
    else if (width >= 1280)
    {
        edid = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28,
                0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x43,
                0x45, 0x08, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
    else
    {
        edid = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x04, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x14, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78, 0x0A, 0xC8, 0x95, 0x9E, 0x57, 0x54, 0x92, 0x26,
                0x0F, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x96, 0x0B, 0x20, 0x8A, 0x30, 0xE0, 0x1B, 0x10, 0x28, 0x30,
                0x33, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B, 0x3D, 0x1D,
                0x20, 0x03, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x42,
                0x65, 0x72, 0x72, 0x79, 0x41, 0x75, 0x74, 0x6F, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    uint8_t sum = 0;
    for (int i = 0; i < 127; ++i)
    {
        sum += edid[i];
    }
    edid[127] = (uint8_t)(256 - sum);
    return edid;
}

void VideoEncoder::handle_evdi_update(int buffer_id)
{
    (void)buffer_id;
}

static void evdi_grab_and_request_next(VideoEncoder* enc, int buf_id)
{
    if (buf_id < 0 || buf_id >= enc->evdi_buffer_count)
        return;
    evdi_buffer& buf = enc->evdi_buffers[buf_id];

    while (true)
    {
        evdi_rect rects[16];
        int num_rects = 16;
        evdi_grab_pixels(enc->evdi, rects, &num_rects);

        if (num_rects > 0 && buf.buffer && enc->input_w > 0 && enc->input_h > 0)
        {
            static bool first_desktop_frame = false;
            if (!first_desktop_frame)
            {
                LOG_I("[EVDI] Received first actual desktop frame! Overwriting dummy screen.");
                first_desktop_frame = true;
            }

            {
                std::lock_guard<std::mutex> lock(enc->frame_mutex);
                size_t req_size = buf.stride * buf.height;
                if (enc->frame_buffers[enc->write_idx].size() != req_size)
                {
                    enc->frame_buffers[enc->write_idx].resize(req_size);
                }
                memcpy(enc->frame_buffers[enc->write_idx].data(), buf.buffer, req_size);
                enc->latest_stride = buf.stride;
                enc->frame_ready = true;
            }
            enc->frame_cv.notify_one();
        }

        // CRITICAL FIX: evdi_request_update returns TRUE if the request is queued successfully.
        // It returns FALSE if the buffer is ALREADY dirty, meaning the compositor updated the
        // screen while we were copying. If it returns false, we MUST NOT break, we must loop
        // and grab the new pixels immediately to catch the final frame of an animation.
        if (evdi_request_update(enc->evdi, buf_id))
        {
            break;
        }
    }
}

static void evdi_update_ready_handler(int buffer_to_be_updated, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    evdi_grab_and_request_next(enc, buffer_to_be_updated);
}

static void evdi_dpms_handler(int dpms_mode, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    LOG_I("[EVDI] DPMS State Changed: " << dpms_mode);

    if (dpms_mode == 0) // DRM_MODE_DPMS_ON
    {
        if (enc->evdi_buffers[0].buffer)
        {
            if (evdi_request_update(enc->evdi, 0))
            {
                evdi_grab_and_request_next(enc, 0);
            }
        }
    }
}

static void evdi_mode_changed_handler(evdi_mode mode, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    LOG_I("[EVDI] Compositor updated Mode: " << mode.width << "x" << mode.height);

    {
        std::lock_guard<std::mutex> lock(enc->frame_mutex);
        enc->input_w = mode.width;
        enc->input_h = mode.height;
        enc->input_fmt = AV_PIX_FMT_BGRA;
        enc->latest_stride = mode.width * 4;

        size_t req_size = enc->latest_stride * enc->input_h;
        enc->frame_buffers[0].resize(req_size);
        enc->frame_buffers[1].resize(req_size);

        // Fill BOTH buffers with the purple dummy screen so swaps don't reveal garbage
        for (size_t i = 0; i < req_size; i += 4)
        {
            enc->frame_buffers[0][i] = 0x80;     // Blue  (128)
            enc->frame_buffers[0][i + 1] = 0x00; // Green (0)
            enc->frame_buffers[0][i + 2] = 0x80; // Red   (128)
            enc->frame_buffers[0][i + 3] = 0xFF; // Alpha (255)

            enc->frame_buffers[1][i] = 0x80;
            enc->frame_buffers[1][i + 1] = 0x00;
            enc->frame_buffers[1][i + 2] = 0x80;
            enc->frame_buffers[1][i + 3] = 0xFF;
        }
        enc->frame_ready = true;
    }

    for (int i = 0; i < enc->evdi_buffer_count; ++i)
    {
        if (enc->evdi_buffers[i].buffer)
        {
            evdi_unregister_buffer(enc->evdi, enc->evdi_buffers[i].id);
            free(enc->evdi_buffers[i].buffer);
        }

        memset(&enc->evdi_buffers[i], 0, sizeof(evdi_buffer));
        enc->evdi_buffers[i].id = i;
        enc->evdi_buffers[i].width = mode.width;
        enc->evdi_buffers[i].height = mode.height;
        enc->evdi_buffers[i].stride = mode.width * 4;
        enc->evdi_buffers[i].buffer = calloc(mode.height, enc->evdi_buffers[i].stride);

        evdi_register_buffer(enc->evdi, enc->evdi_buffers[i]);
    }

    enc->update_sws();

    // Prime the pump using buffer 0 ONLY
    if (evdi_request_update(enc->evdi, 0))
    {
        evdi_grab_and_request_next(enc, 0);
    }
}

static void evdi_crtc_state_handler(int state, void* user_data)
{
    (void)state; // Suppress unused parameter warning
    (void)user_data;
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
        memset(&evdi_buffers[i], 0, sizeof(evdi_buffer));
        evdi_buffers[i].id = i;
        evdi_buffers[i].buffer = nullptr;
    }

    std::vector<uint8_t> edid = get_edid(target_width, target_height);
    evdi_connect(evdi, edid.data(), edid.size(), 3840 * 2160);
    LOG_I("[EVDI] Virtual Monitor connected: " << target_width << "x" << target_height);

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

    // Constant FPS Loop Initialization
    auto frame_duration = std::chrono::milliseconds(1000 / target_fps);
    auto next_tick = std::chrono::steady_clock::now() + frame_duration;

    while (running.load())
    {
        int current_stride = 0, current_w = 0, current_h = 0;
        bool do_process = false;

        {
            std::unique_lock<std::mutex> lock(frame_mutex);
            // Wait up to the frame duration for a NEW frame from EVDI or a manual keyframe request
            frame_cv.wait_until(lock, next_tick,
                                [this] { return frame_ready || request_keyframe.load() || !running.load(); });

            if (!running.load())
                break;

            if (frame_ready)
            {
                // Only swap if we actually got new pixels from the compositor
                std::swap(write_idx, read_idx);
                frame_ready = false;
                do_process = true;
            }
            else
            {
                // Timeout reached or keyframe requested.
                // We MUST process the old frame (read_idx) to flush hardware encoders
                // like v4l2m2m that hold frames hostage in their pipeline.
                do_process = true;
            }

            if (do_process)
            {
                current_w = input_w;
                current_h = input_h;
                current_stride = latest_stride;
            }
        }

        if (do_process && current_w > 0 && current_h > 0)
        {
            process_raw_frame(frame_buffers[read_idx].data(), current_stride, current_w, current_h);
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next_tick)
        {
            next_tick = now + frame_duration;
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
