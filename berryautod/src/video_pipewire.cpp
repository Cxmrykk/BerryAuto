#include "globals.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <linux/dma-buf.h>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

extern uint64_t get_monotonic_usec();

static int pw_frame_count = 0;

static void on_process(void* userdata)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);
    struct pw_buffer* b = pw_stream_dequeue_buffer(enc->pw_stream_inst);
    if (!b)
        return;

    struct spa_buffer* buf = b->buffer;
    void* src_data = buf->datas[0].data;

    if (src_data)
    {
        std::lock_guard<std::mutex> lock(enc->frame_mutex);
        uint32_t size = buf->datas[0].chunk->size;
        uint32_t stride = buf->datas[0].chunk->stride;

        if (size == 0)
            size = buf->datas[0].maxsize;
        if (stride == 0)
            stride = enc->pw_w * 4;
        if (size > buf->datas[0].maxsize)
            size = buf->datas[0].maxsize;

        if (size > 0 && !(buf->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
        {
            pw_frame_count++;
            if (pw_frame_count == 1)
                LOG_I("[PipeWire] SUCCESS! Extracted first frame! (Size: " << size << ")");

            if (enc->latest_frame_buffer.size() != (size_t)size)
                enc->latest_frame_buffer.resize(size);

            memcpy(enc->latest_frame_buffer.data(), src_data, size);
            enc->latest_stride = stride;
        }
    }
    else
    {
        LOG_E("[PipeWire] Buffer data pointer is NULL!");
    }

    pw_stream_queue_buffer(enc->pw_stream_inst, b);
}

static void on_param_changed(void* userdata, uint32_t id, const struct spa_pod* param)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);

    if (param == NULL || id != SPA_PARAM_Format)
        return;

    struct spa_video_info_raw info;
    if (spa_format_video_raw_parse(param, &info) >= 0)
    {
        LOG_I("[PipeWire] Negotiated Format -> " << info.size.width << "x" << info.size.height);

        {
            std::lock_guard<std::mutex> lock(enc->frame_mutex);
            enc->pw_w = info.size.width;
            enc->pw_h = info.size.height;

            switch (info.format)
            {
                case SPA_VIDEO_FORMAT_RGBx:
                case SPA_VIDEO_FORMAT_RGBA:
                    enc->pw_fmt = AV_PIX_FMT_RGB0;
                    break;
                case SPA_VIDEO_FORMAT_BGRx:
                case SPA_VIDEO_FORMAT_BGRA:
                    enc->pw_fmt = AV_PIX_FMT_BGR0;
                    break;
                case SPA_VIDEO_FORMAT_RGB:
                    enc->pw_fmt = AV_PIX_FMT_RGB24;
                    break;
                case SPA_VIDEO_FORMAT_BGR:
                    enc->pw_fmt = AV_PIX_FMT_BGR24;
                    break;
                default:
                    enc->pw_fmt = AV_PIX_FMT_BGRA;
                    break;
            }
            enc->latest_frame_buffer.clear();
        }

        enc->update_sws();

        uint32_t stride = (info.size.width * 4 + 3) & ~3;
        if (info.format == SPA_VIDEO_FORMAT_RGB || info.format == SPA_VIDEO_FORMAT_BGR)
            stride = (info.size.width * 3 + 3) & ~3;

        uint32_t size = stride * info.size.height;

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const struct spa_pod* params[1];

        params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
            &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size,
            SPA_POD_Int(size), SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), SPA_PARAM_BUFFERS_align, SPA_POD_Int(16),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemPtr), 0);

        pw_stream_update_params(enc->pw_stream_inst, params, 1);
    }
}

static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    (void)userdata;
    (void)old;
    if (error)
        LOG_E("[PipeWire] Stream Error: " << error);
}

static const struct pw_stream_events stream_events = []()
{
    struct pw_stream_events ev{};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.process = on_process;
    ev.state_changed = on_state_changed;
    ev.param_changed = on_param_changed;
    return ev;
}();

bool VideoEncoder::init_pipewire(uint32_t node_id, int pw_fd)
{
    LOG_I("[PipeWire] Connecting to target Node ID: " << node_id << " (FD: " << pw_fd << ")");

    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
        return false;

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core_inst = (pw_fd >= 0) ? pw_context_connect_fd(pw_ctx, pw_fd, NULL, 0) : pw_context_connect(pw_ctx, NULL, 0);

    if (!pw_core_inst)
        return false;

    struct pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                    PW_KEY_MEDIA_ROLE, "Screen", NULL);

    pw_stream_inst = pw_stream_new(pw_core_inst, "BerryAuto Capture", props);
    if (!pw_stream_inst)
        return false;

    pw_stream_add_listener(pw_stream_inst, &stream_listener, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];

    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);

    int res = pw_stream_connect(pw_stream_inst, PW_DIRECTION_INPUT, node_id,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);

    return (res >= 0);
}

void VideoEncoder::cleanup_pipewire()
{
    if (pw_stream_inst)
        pw_stream_destroy(pw_stream_inst);
    if (pw_core_inst)
        pw_core_disconnect(pw_core_inst);
    if (pw_ctx)
        pw_context_destroy(pw_ctx);
    if (pw_loop)
        pw_main_loop_destroy(pw_loop);

    pw_stream_inst = nullptr;
    pw_core_inst = nullptr;
    pw_ctx = nullptr;
    pw_loop = nullptr;

    close_wayland_screencast();
}

void VideoEncoder::run_pipewire_loop(uint32_t node_id, int pw_fd)
{
    pw_init(NULL, NULL);
    if (!init_pipewire(node_id, pw_fd))
    {
        pw_deinit();
        return;
    }

    std::thread pw_encoder_thread(
        [this]()
        {
            uint64_t frame_interval_us = 1000000 / target_fps;
            uint64_t next_frame_time = get_monotonic_usec() + frame_interval_us;
            int wake_timer = 0;

            while (running.load())
            {
                wake_timer++;
                if (wake_timer >= target_fps)
                {
                    wake_up_display();
                    wake_timer = 0;
                }

                std::vector<uint8_t> frame_copy;
                int current_stride = 0, current_w = 0, current_h = 0;

                {
                    std::lock_guard<std::mutex> lock(frame_mutex);
                    current_w = pw_w;
                    current_h = pw_h;

                    if (latest_frame_buffer.empty() && current_w > 0 && current_h > 0)
                    {
                        int req = av_image_get_buffer_size(pw_fmt, current_w, current_h, 1);
                        if (req > 0)
                        {
                            latest_frame_buffer.resize(req, 0);
                            latest_stride = current_w * 4;
                        }
                    }
                    frame_copy = latest_frame_buffer;
                    current_stride = latest_stride;
                }

                if (!frame_copy.empty() && current_w > 0 && current_h > 0)
                {
                    process_raw_frame(frame_copy.data(), current_stride, current_w, current_h);
                }

                uint64_t now = get_monotonic_usec();
                if (now < next_frame_time)
                    usleep(next_frame_time - now);
                next_frame_time = get_monotonic_usec() + frame_interval_us;
            }
        });

    wake_up_display();
    pw_main_loop_run(pw_loop);

    running = false;

    pw_main_loop_quit(pw_loop);

    if (pw_encoder_thread.joinable())
        pw_encoder_thread.join();
    cleanup_pipewire();
    pw_deinit();
}
