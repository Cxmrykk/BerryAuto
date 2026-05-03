#include "globals.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

extern uint64_t get_monotonic_usec();

static void frame_handle_buffer(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width,
                                uint32_t height, uint32_t stride)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(data);

    size_t size = stride * height;

    int fd = syscall(SYS_memfd_create, "wayland-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
    {
        LOG_E("[Capture] memfd_create failed.");
        return;
    }
    ftruncate(fd, size);
    enc->current_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    wl_shm_pool* pool = wl_shm_create_pool(enc->wl_shm_inst, fd, size);
    enc->current_buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);

    enc->current_size = size;
    enc->pw_w = width;
    enc->pw_h = height;
    enc->latest_stride = stride;

    switch (format)
    {
        case WL_SHM_FORMAT_XRGB8888:
            enc->pw_fmt = AV_PIX_FMT_BGR0;
            break;
        case WL_SHM_FORMAT_ARGB8888:
            enc->pw_fmt = AV_PIX_FMT_BGRA;
            break;
        case WL_SHM_FORMAT_XBGR8888:
            enc->pw_fmt = AV_PIX_FMT_RGB0;
            break;
        case WL_SHM_FORMAT_ABGR8888:
            enc->pw_fmt = AV_PIX_FMT_RGBA;
            break;
        default:
            enc->pw_fmt = AV_PIX_FMT_BGR0;
            break;
    }

    enc->update_sws();
    zwlr_screencopy_frame_v1_copy(frame, enc->current_buffer);
    close(fd);
}

static void frame_handle_flags(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t flags)
{
    (void)data;
    (void)frame;
    (void)flags;
}

static void frame_handle_ready(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi,
                               uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    VideoEncoder* enc = static_cast<VideoEncoder*>(data);

    {
        std::lock_guard<std::mutex> lock(enc->frame_mutex);
        if (enc->latest_frame_buffer.size() != enc->current_size)
        {
            enc->latest_frame_buffer.resize(enc->current_size);
        }
        if (enc->current_data && enc->current_data != MAP_FAILED)
        {
            memcpy(enc->latest_frame_buffer.data(), enc->current_data, enc->current_size);
        }
    }

    if (enc->current_data && enc->current_data != MAP_FAILED)
    {
        munmap(enc->current_data, enc->current_size);
    }
    if (enc->current_buffer)
    {
        wl_buffer_destroy(enc->current_buffer);
    }

    enc->current_data = nullptr;
    enc->current_buffer = nullptr;
    enc->frame_ready = true;
}

static void frame_handle_failed(void* data, struct zwlr_screencopy_frame_v1* frame)
{
    (void)frame;
    VideoEncoder* enc = static_cast<VideoEncoder*>(data);

    if (enc->current_data && enc->current_data != MAP_FAILED)
    {
        munmap(enc->current_data, enc->current_size);
    }
    if (enc->current_buffer)
    {
        wl_buffer_destroy(enc->current_buffer);
    }

    enc->current_data = nullptr;
    enc->current_buffer = nullptr;
    enc->frame_ready = true;
    LOG_E("[Capture] wlr-screencopy failed to acquire frame.");
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
};

void VideoEncoder::request_wayland_frame_sync()
{
    frame_ready = false;
    current_data = nullptr;
    current_buffer = nullptr;

    zwlr_screencopy_frame_v1* frame = zwlr_screencopy_manager_v1_capture_output(wlr_screencopy, 0, wl_out);
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, this);

    while (!frame_ready)
    {
        if (wl_display_dispatch(wl_dpy) < 0)
        {
            LOG_E("[Capture] wl_display_dispatch failed.");
            break;
        }
    }
    zwlr_screencopy_frame_v1_destroy(frame);
}

static void registry_handle_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface,
                                   uint32_t version)
{
    (void)version;
    VideoEncoder* enc = static_cast<VideoEncoder*>(data);

    if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        enc->wl_shm_inst = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, wl_output_interface.name) == 0)
    {
        if (!enc->wl_out)
        {
            enc->wl_out = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 1);
        }
    }
    else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0)
    {
        enc->wlr_screencopy =
            (zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
        enc->has_wlr_screencopy = true;
    }
}

static void registry_handle_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

bool VideoEncoder::init_wlr_registry()
{
    wl_dpy = wl_display_connect(NULL);
    if (!wl_dpy)
        return false;

    wl_reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(wl_reg, &registry_listener, this);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    return true;
}

void VideoEncoder::cleanup_wlr_registry()
{
    if (wlr_screencopy)
        zwlr_screencopy_manager_v1_destroy(wlr_screencopy);
    if (wl_out)
        wl_output_destroy(wl_out);
    if (wl_shm_inst)
        wl_shm_destroy(wl_shm_inst);
    if (wl_reg)
        wl_registry_destroy(wl_reg);
    if (wl_dpy)
        wl_display_disconnect(wl_dpy);
}

void VideoEncoder::run_wlr_loop()
{
    uint64_t frame_interval_us = 1000000 / target_fps;
    uint64_t next_frame_time = get_monotonic_usec() + frame_interval_us;
    int wake_timer = 0;

    wake_up_display();

    while (running.load())
    {
        wake_timer++;
        if (wake_timer >= target_fps)
        {
            wake_up_display();
            wake_timer = 0;
        }

        request_wayland_frame_sync();

        std::vector<uint8_t> frame_copy;
        int current_stride = 0;
        int current_w = 0;
        int current_h = 0;

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            current_w = pw_w;
            current_h = pw_h;
            frame_copy = latest_frame_buffer;
            current_stride = latest_stride;
        }

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
}
