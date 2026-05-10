#include "globals.hpp"
#include "video_encoder.hpp"
#include <cstring>
#include <iostream>
#include <sys/select.h>
#include <vector>

// CVT Base EDID Template
std::vector<uint8_t> VideoEncoder::generate_edid(int width, int height, int fps)
{
    // A standard 128-byte EDID. We patch the Detailed Timing Descriptor (DTD) dynamically.
    std::vector<uint8_t> edid = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x09, 0x69, 0x12, 0x34, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
        0x01, 0x04, 0x95, 0x21, 0x13, 0x78, 0xea, 0x8d, 0x85, 0xa6, 0x54, 0x4a, 0x9c, 0x26, 0x12, 0x50, 0x54, 0x00,
        0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        // DTD Block starts at index 54
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Descriptor 2 (Monitor Name)
        0x00, 0x00, 0x00, 0xfc, 0x00, 'B', 'e', 'r', 'r', 'y', 'A', 'u', 't', 'o', 0x0a, 0x20, 0x20, 0x20,
        // Descriptor 3 (Unused)
        0x00, 0x00, 0x00, 0xfd, 0x00, 0x32, 0x4c, 0x1e, 0x53, 0x11, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        // Descriptor 4 (Unused)
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00 // Extension block count + checksum (replaced later)
    };

    // Calculate basic CVT timing values
    int h_blank = width * 0.25;
    int v_blank = 30;
    int pixel_clock = ((width + h_blank) * (height + v_blank) * fps) / 10000;

    edid[54] = pixel_clock & 0xFF;
    edid[55] = (pixel_clock >> 8) & 0xFF;

    edid[56] = width & 0xFF;
    edid[57] = h_blank & 0xFF;
    edid[58] = (((width >> 8) & 0x0F) << 4) | ((h_blank >> 8) & 0x0F);

    edid[59] = height & 0xFF;
    edid[60] = v_blank & 0xFF;
    edid[61] = (((height >> 8) & 0x0F) << 4) | ((v_blank >> 8) & 0x0F);

    // Sync offsets / pulse widths (Dummy values for virtual display)
    edid[62] = 0x20;
    edid[63] = 0x20;
    edid[64] = 0x20;
    edid[65] = 0x20;

    edid[66] = 0x10; // Image Size H
    edid[67] = 0x09; // Image Size V
    edid[68] = 0x00;

    edid[69] = 0x00; // Border
    edid[70] = 0x00;
    edid[71] = 0x18; // Features

    // Checksum
    uint8_t sum = 0;
    for (int i = 0; i < 127; ++i)
        sum += edid[i];
    edid[127] = (256 - sum) & 0xFF;

    return edid;
}

static void evdi_dpms_handler(int dpms_mode, void* user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    LOG_I("[EVDI] DPMS State Changed: " << dpms_mode);
    if (dpms_mode == 0)
    {
        // Monitor is ON, request initial frames
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

    enc->input_w = mode.width;
    enc->input_h = mode.height;

    // EVDI defaults to BGRA or RGBA.
    // We assume 32-bit BGRA (typical Linux framebuffer format for virtual outputs)
    enc->input_fmt = AV_PIX_FMT_BGRA;
    enc->update_sws();

    // Reallocate buffers
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

        // Grab the pixels that changed
        evdi_rect rects[16];
        int num_rects = 0;
        evdi_grab_pixels(enc->evdi, rects, &num_rects);

        // Process the full buffer (we could optimize using rects later)
        if (buf.buffer && enc->input_w > 0 && enc->input_h > 0)
        {
            enc->process_raw_frame(buf.buffer, buf.stride, enc->input_w, enc->input_h);
        }

        // Return the buffer to the pool so EVDI can draw into it again
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
    // Internal delegate method
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
        LOG_E("[EVDI] CRITICAL: No available EVDI devices found! Did you `modprobe evdi`?");
        return;
    }

    evdi = evdi_open(device_num);
    if (evdi == EVDI_INVALID_HANDLE)
    {
        LOG_E("[EVDI] Failed to open device /dev/dri/card" << device_num);
        return;
    }

    // Set up event handlers
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

    // Generate dynamic EDID based on negotiated Android Auto resolution
    std::vector<uint8_t> edid = generate_edid(target_width, target_height, target_fps);

    // Connect the virtual monitor!
    evdi_connect(evdi, edid.data(), edid.size(), 0);
    LOG_I("[EVDI] Virtual Monitor connected: " << target_width << "x" << target_height);

    int evdi_fd = evdi_get_event_ready(evdi);
    fd_set rfds;
    struct timeval tv;

    // Main EVDI event loop
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

    // Disconnect and Cleanup
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
