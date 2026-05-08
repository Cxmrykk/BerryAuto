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
    struct pw_buffer* b = pw_stream_dequeue_buffer(enc->pw_stream);
    if (!b)
    {
        LOG_E("[PipeWire Debug] on_process: pw_stream_dequeue_buffer returned NULL!");
        return;
    }

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
            else if (pw_frame_count % 60 == 0)
                LOG_I("[PipeWire] Heartbeat: Extracted " << pw_frame_count << " healthy frames...");

            if (enc->latest_frame_buffer.size() != (size_t)size)
                enc->latest_frame_buffer.resize(size);

            memcpy(enc->latest_frame_buffer.data(), src_data, size);
            enc->latest_stride = stride;
        }
    }
    else
    {
        LOG_E("[PipeWire Debug] on_process: Buffer data pointer is NULL!");
    }

    pw_stream_queue_buffer(enc->pw_stream, b);
}

static void on_param_changed(void* userdata, uint32_t id, const struct spa_pod* param)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);

    if (param == NULL)
    {
        LOG_E("[PipeWire Debug] param_changed: FORMAT CLEARED! (id = "
              << id << "). "
              << "This means the Node rejected our format or disconnected!");
        return;
    }

    if (id == SPA_PARAM_EnumFormat)
    {
        LOG_I("[PipeWire Debug] param_changed: Server proposing SPA_PARAM_EnumFormat...");
        return;
    }

    if (id != SPA_PARAM_Format)
    {
        LOG_I("[PipeWire Debug] param_changed: Unhandled param ID = " << id);
        return;
    }

    LOG_I("[PipeWire Debug] param_changed: Server accepted SPA_PARAM_Format! Parsing Layout...");

    struct spa_video_info_raw info;
    int parse_res = spa_format_video_raw_parse(param, &info);

    if (parse_res >= 0)
    {
        LOG_I("[PipeWire] Native Format negotiated! Size: " << info.size.width << "x" << info.size.height
                                                            << ", SPA ID: " << info.format);

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
                case SPA_VIDEO_FORMAT_xBGR:
                case SPA_VIDEO_FORMAT_ABGR:
                    enc->pw_fmt = AV_PIX_FMT_0BGR;
                    break;
                case SPA_VIDEO_FORMAT_xRGB:
                case SPA_VIDEO_FORMAT_ARGB:
                    enc->pw_fmt = AV_PIX_FMT_0RGB;
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

        LOG_I("[PipeWire Debug] param_changed: Requesting Memory Buffers. Size: " << size << ", Stride: " << stride);

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const struct spa_pod* params[1];

        params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
            &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size,
            SPA_POD_Int(size), SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), SPA_PARAM_BUFFERS_align, SPA_POD_Int(16),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)));

        int update_res = pw_stream_update_params(enc->pw_stream, params, 1);
        if (update_res < 0)
        {
            LOG_E("[PipeWire Debug] pw_stream_update_params failed: " << spa_strerror(update_res));
        }
        else
        {
            LOG_I("[PipeWire Debug] Memory Buffers requested successfully.");
        }
    }
    else
    {
        LOG_E("[PipeWire Debug] CRITICAL: spa_format_video_raw_parse failed with code "
              << parse_res << ". Format contains unsupported modifiers or DMA-BUF specifics we cannot handle!");
    }
}

static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    (void)userdata;
    if (error)
        LOG_E("[PipeWire Debug] Stream Error! Transition: " << pw_stream_state_as_string(old) << " -> "
                                                            << pw_stream_state_as_string(state)
                                                            << " | Reason: " << error);
    else
        LOG_I("[PipeWire Debug] Stream State changed: " << pw_stream_state_as_string(old) << " -> "
                                                        << pw_stream_state_as_string(state));
}

static void on_add_buffer(void* userdata, struct pw_buffer* b)
{
    LOG_I("[PipeWire Debug] on_add_buffer: Server allocated buffer map.");
}

static void on_remove_buffer(void* userdata, struct pw_buffer* b)
{
    LOG_I("[PipeWire Debug] on_remove_buffer: Server destroyed buffer map.");
}

static void on_io_changed(void* userdata, uint32_t id, void* area, uint32_t size)
{
    LOG_I("[PipeWire Debug] on_io_changed: ID = " << id << " | Size = " << size);
}

static const struct pw_stream_events stream_events = []()
{
    struct pw_stream_events ev{};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.process = on_process;
    ev.state_changed = on_state_changed;
    ev.param_changed = on_param_changed;
    ev.add_buffer = on_add_buffer;
    ev.remove_buffer = on_remove_buffer;
    ev.io_changed = on_io_changed;
    return ev;
}();

// --- Core Logging Handlers ---
static void on_core_info(void* userdata, const struct pw_core_info* info)
{
    LOG_I("[PipeWire Debug] Core Info: Name=" << (info->name ? info->name : "Unknown")
                                              << ", Version=" << (info->version ? info->version : "Unknown"));
}

static void on_core_error(void* userdata, uint32_t id, int seq, int res, const char* message)
{
    LOG_E("[PipeWire Debug] Core Error! ID: " << id << ", Seq: " << seq << ", Res: " << res << " (" << spa_strerror(res)
                                              << "), Message: " << (message ? message : "None"));
}

static void on_core_done(void* userdata, uint32_t id, int seq)
{
    LOG_I("[PipeWire Debug] Core Done! ID: " << id << ", Seq: " << seq);
}

static const struct pw_core_events core_events = []()
{
    struct pw_core_events ev{};
    ev.version = PW_VERSION_CORE_EVENTS;
    ev.info = on_core_info;
    ev.done = on_core_done;
    ev.error = on_core_error;
    return ev;
}();

bool VideoEncoder::init_pipewire(uint32_t node_id, int pw_fd)
{
    LOG_I("[PipeWire Debug] Initializing... Connecting to Node ID: " << node_id << " with FD: " << pw_fd);
    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
        return false;

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);

    if (pw_fd >= 0)
        pw_core = pw_context_connect_fd(pw_ctx, pw_fd, NULL, 0);
    else
        pw_core = pw_context_connect(pw_ctx, NULL, 0);

    if (!pw_core)
    {
        LOG_E("[PipeWire Debug] Failed to connect context to core server.");
        return false;
    }

    spa_zero(core_listener);
    pw_core_add_listener(pw_core, &core_listener, &core_events, this);

    std::string node_id_str = std::to_string(node_id);

    pw_stream =
        pw_stream_new(pw_core, "OpenGAL Capture",
                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
                                        "Screen", PW_KEY_TARGET_OBJECT, node_id_str.c_str(), NULL));

    if (!pw_stream)
    {
        LOG_E("[PipeWire Debug] pw_stream_new failed.");
        return false;
    }

    spa_zero(stream_listener);
    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const struct spa_pod* params[1];

    struct spa_rectangle def_rect =
        SPA_RECTANGLE(static_cast<uint32_t>(target_width), static_cast<uint32_t>(target_height));
    struct spa_rectangle min_rect = SPA_RECTANGLE(1, 1);
    struct spa_rectangle max_rect = SPA_RECTANGLE(16384, 16384);

    struct spa_fraction def_frac = SPA_FRACTION(static_cast<uint32_t>(target_fps), 1);
    struct spa_fraction min_frac = SPA_FRACTION(0, 1);
    struct spa_fraction max_frac = SPA_FRACTION(1000, 1);

    LOG_I("[PipeWire Debug] Formatting EnumFormat constraints for negotiation...");

    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(7, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA,
                               SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_BGR,
                               SPA_VIDEO_FORMAT_RGB),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def_rect, &min_rect, &max_rect),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&def_frac, &min_frac, &max_frac));

    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);

    if (res < 0)
    {
        LOG_E("[PipeWire Debug] pw_stream_connect failed! Error: " << spa_strerror(res));
        return false;
    }

    LOG_I("[PipeWire Debug] pw_stream_connect dispatched successfully (" << res
                                                                         << "). Awaiting Server Node Response...");
    return true;
}

void VideoEncoder::cleanup_pipewire()
{
    if (pw_stream)
        pw_stream_destroy(pw_stream);
    if (pw_core)
        pw_core_disconnect(pw_core);
    if (pw_ctx)
        pw_context_destroy(pw_ctx);
    if (pw_loop)
        pw_main_loop_destroy(pw_loop);
    pw_stream = nullptr;
    pw_core = nullptr;
    pw_ctx = nullptr;
    pw_loop = nullptr;
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
                int current_stride = 0;
                int current_w = 0;
                int current_h = 0;

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
                {
                    usleep(next_frame_time - now);
                    next_frame_time += frame_interval_us;
                }
                else
                {
                    next_frame_time = now + frame_interval_us;
                }
            }
        });

    wake_up_display();
    pw_main_loop_run(pw_loop);

    running = false;
    if (pw_encoder_thread.joinable())
        pw_encoder_thread.join();
    cleanup_pipewire();
    pw_deinit();
}
