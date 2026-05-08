#include "globals.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <linux/dma-buf.h>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
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
        LOG_E("[PipeWire Debug] on_process: Buffer data pointer is NULL! (Did we receive a raw DMA-BUF without "
              "mapping?)");
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
              << "The Server Node rejected our format intersection or abruptly disconnected!");
        return;
    }

    if (id == SPA_PARAM_EnumFormat)
    {
        LOG_I("[PipeWire Debug] param_changed: Server sent an EnumFormat constraint check.");
        return;
    }

    if (id != SPA_PARAM_Format)
    {
        LOG_I("[PipeWire Debug] param_changed: Unhandled param ID = " << id);
        return;
    }

    LOG_I("[PipeWire Debug] param_changed: Server ACCEPTED a Format! Parsing Layout...");

    struct spa_video_info_raw info;
    int parse_res = spa_format_video_raw_parse(param, &info);

    if (parse_res >= 0)
    {
        LOG_I("[PipeWire] Negotiated Format -> Size: " << info.size.width << "x" << info.size.height
                                                       << " | SPA ID: " << info.format);

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

        LOG_I("[PipeWire Debug] param_changed: Requesting Memory Buffers. Block Size: " << size
                                                                                        << ", Stride: " << stride);

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const struct spa_pod* params[1];

        params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
            &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size,
            SPA_POD_Int(size), SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), SPA_PARAM_BUFFERS_align, SPA_POD_Int(16),
            SPA_PARAM_BUFFERS_dataType,
            SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_DmaBuf)));

        int update_res = pw_stream_update_params(enc->pw_stream, params, 1);
        if (update_res < 0)
        {
            LOG_E("[PipeWire Debug] pw_stream_update_params failed: " << spa_strerror(update_res));
        }
        else
        {
            LOG_I("[PipeWire Debug] Memory Buffers requested successfully. Awaiting streaming state...");
        }
    }
    else
    {
        LOG_E("[PipeWire Debug] CRITICAL: spa_format_video_raw_parse failed ("
              << parse_res << "). "
              << "The Server returned unsupported modifiers or a non-raw video layout.");
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
    (void)userdata;
    (void)b;
    LOG_I("[PipeWire Debug] on_add_buffer: Server allocated buffer map.");
}

static void on_remove_buffer(void* userdata, struct pw_buffer* b)
{
    (void)userdata;
    (void)b;
    LOG_I("[PipeWire Debug] on_remove_buffer: Server destroyed buffer map.");
}

static void on_io_changed(void* userdata, uint32_t id, void* area, uint32_t size)
{
    (void)userdata;
    (void)area;
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
    (void)userdata;
    LOG_I("[PipeWire Debug] Core Info: Name=" << (info->name ? info->name : "Unknown")
                                              << ", Version=" << (info->version ? info->version : "Unknown"));
}

static void on_core_error(void* userdata, uint32_t id, int seq, int res, const char* message)
{
    (void)userdata;
    LOG_E("[PipeWire Debug] Core Error! ID: " << id << ", Seq: " << seq << ", Res: " << res << " (" << spa_strerror(res)
                                              << "), Message: " << (message ? message : "None"));
}

static void on_core_done(void* userdata, uint32_t id, int seq)
{
    (void)userdata;
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
    setenv("PIPEWIRE_DEBUG", "4", 1);
    setenv("WIREPLUMBER_DEBUG", "4", 1);
    setenv("SPA_DEBUG", "4", 1);

    LOG_I("[PipeWire Debug] Initializing... Connecting to target Node ID: " << node_id << " (FD: " << pw_fd << ")");

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
        LOG_E("[PipeWire Debug] Failed to connect Context to Core Server!");
        return false;
    }

    spa_zero(core_listener);
    pw_core_add_listener(pw_core, &core_listener, &core_events, this);

    // CRITICAL FIX 1: Do NOT set PW_KEY_TARGET_OBJECT.
    // The Portal sandbox already locked this connection to the screen sharing node.
    struct pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                    PW_KEY_MEDIA_ROLE, "Screen", NULL);

    pw_stream = pw_stream_new(pw_core, "BerryAuto Capture", props);

    if (!pw_stream)
    {
        LOG_E("[PipeWire Debug] pw_stream_new failed!");
        return false;
    }

    spa_zero(stream_listener);
    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];

    LOG_I("[PipeWire Debug] Formatting broad EnumFormat constraints to force a server reply...");

    // CRITICAL FIX 2: Omit size and framerate boundaries entirely.
    // Providing constraints that do not mathematically intersect with Mutter's private
    // virtual display layout will result in an empty intersection and a permanent pause.
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(7,
                               SPA_VIDEO_FORMAT_RGBx, // Default
                               SPA_VIDEO_FORMAT_RGBx, // Choices...
                               SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
                               SPA_VIDEO_FORMAT_RGB, SPA_VIDEO_FORMAT_BGR));

    if (!params[0])
    {
        LOG_E("[PipeWire Debug] CRITICAL: spa_pod_builder_add_object failed to allocate EnumFormat!");
        return false;
    }

    // CRITICAL FIX 3: Connect using PW_ID_ANY.
    // Specifying the node_id explicitly on a sandboxed portal FD is an illegal graph mutation.
    // WirePlumber will reject the format negotiation if you try to route it explicitly here.
    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);

    if (res < 0)
    {
        LOG_E("[PipeWire Debug] pw_stream_connect failed! Error: " << spa_strerror(res));
        return false;
    }

    LOG_I("[PipeWire Debug] pw_stream_connect dispatched successfully (" << res << ") for Target Node " << node_id);
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
