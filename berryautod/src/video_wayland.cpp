#include "globals.hpp"
#include "video_encoder.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <sys/mman.h>

static int pw_frame_count = 0;

static void on_process(void* userdata)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);
    struct pw_buffer* b = pw_stream_dequeue_buffer(enc->pw_stream);
    if (!b)
        return;

    struct spa_buffer* buf = b->buffer;
    void* src_data = buf->datas[0].data;
    bool mapped_dmabuf = false;

    // --- CRITICAL FALLBACK: Manually map DmaBuf if PipeWire skipped it ---
    if (!src_data && buf->datas[0].type == SPA_DATA_DmaBuf && buf->datas[0].fd >= 0)
    {
        src_data = mmap(NULL, buf->datas[0].maxsize, PROT_READ, MAP_SHARED, buf->datas[0].fd, buf->datas[0].mapoffset);
        if (src_data == MAP_FAILED)
        {
            LOG_E("[PipeWire] CRITICAL: DmaBuf mmap failed! " << strerror(errno));
            src_data = nullptr;
        }
        else
            mapped_dmabuf = true;
    }

    if (src_data)
    {
        std::lock_guard<std::mutex> lock(enc->frame_mutex);
        int size = buf->datas[0].chunk->size;
        int stride = buf->datas[0].chunk->stride;

        if (size > 0 && !(buf->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
        {
            pw_frame_count++;
            if (pw_frame_count == 1)
                LOG_I("[PipeWire] SUCCESS! Extracted first frame! (Size: " << size << " bytes, Stride: " << stride
                                                                           << ")");
            else if (pw_frame_count % 60 == 0)
                LOG_I("[PipeWire] Heartbeat: Receiving healthy frames... (Total: " << pw_frame_count << ")");

            if (enc->latest_frame_buffer.size() != (size_t)size)
                enc->latest_frame_buffer.resize(size);

            memcpy(enc->latest_frame_buffer.data(), src_data, size);
            enc->latest_stride = stride;
        }

        if (mapped_dmabuf)
            munmap(src_data, buf->datas[0].maxsize);
    }
    else
    {
        LOG_E("[PipeWire] WARNING: Received unreadable buffer! Type: " << buf->datas[0].type);
    }

    pw_stream_queue_buffer(enc->pw_stream, b);
}

static void on_param_changed(void* userdata, uint32_t id, const struct spa_pod* param)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);
    if (param == NULL || id != SPA_PARAM_Format)
        return;

    struct spa_video_info_raw info;
    if (spa_format_video_raw_parse(param, &info) >= 0)
    {
        LOG_I("[PipeWire] Format negotiated successfully! Size: " << info.size.width << "x" << info.size.height
                                                                  << ", SPA Format ID: " << info.format);

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
                case SPA_VIDEO_FORMAT_NV12:
                    enc->pw_fmt = AV_PIX_FMT_NV12;
                    break;
                case SPA_VIDEO_FORMAT_I420:
                    enc->pw_fmt = AV_PIX_FMT_YUV420P;
                    break;
                default:
                    LOG_E("[PipeWire] Unrecognized pixel format! Defaulting to BGRA.");
                    enc->pw_fmt = AV_PIX_FMT_BGRA;
                    break;
            }
            enc->latest_frame_buffer.clear();
        }

        enc->update_sws();

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const struct spa_pod* params[1];

        // CRITICAL FIX: Omit 'size' and 'stride' to allow the server to allocate hardware-aligned padded memory
        params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
            &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_dataType,
            SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)));

        pw_stream_update_params(enc->pw_stream, params, 1);
        LOG_I("[PipeWire] Buffer requirements pushed (Allowing Server-Side Alignment). Waiting for data...");
    }
}

static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    if (error)
        LOG_E("[PipeWire] Stream Error: " << error);
    else
        LOG_I("[PipeWire] Stream State changed to: " << pw_stream_state_as_string(state));
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

bool VideoEncoder::init_pipewire(uint32_t node_id)
{
    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
        return false;

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core = pw_context_connect(pw_ctx, NULL, 0);
    if (!pw_core)
        return false;

    pw_stream =
        pw_stream_new(pw_core, "OpenGAL Capture",
                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
                                        "Screen", PW_KEY_TARGET_OBJECT, std::to_string(node_id).c_str(), NULL));

    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    alignas(8) uint8_t buffer[2048];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[2];

    struct spa_rectangle def_rect = {(uint32_t)target_width, (uint32_t)target_height};
    struct spa_rectangle min_rect = {1, 1};
    struct spa_rectangle max_rect = {8192, 8192};
    struct spa_fraction def_frac = {(uint32_t)target_fps, 1};
    struct spa_fraction min_frac = {1, 1};
    struct spa_fraction max_frac = {144, 1};

    // CRITICAL FIX: Use Ranges for Resolution so Wayland isn't forced to reject the stream if it's off by 1 pixel
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(17, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx,
                               SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_xBGR, SPA_VIDEO_FORMAT_ABGR,
                               SPA_VIDEO_FORMAT_xRGB, SPA_VIDEO_FORMAT_ARGB, SPA_VIDEO_FORMAT_RGB, SPA_VIDEO_FORMAT_BGR,
                               SPA_VIDEO_FORMAT_NV12, SPA_VIDEO_FORMAT_I420, SPA_VIDEO_FORMAT_YUY2,
                               SPA_VIDEO_FORMAT_UYVY, SPA_VIDEO_FORMAT_YVYU, SPA_VIDEO_FORMAT_VYUY),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def_rect, &min_rect, &max_rect),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&def_frac, &min_frac, &max_frac));

    // Notice we omitted 'size' and 'stride' here too
    params[1] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_dataType,
        SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)));

    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 2);

    if (res < 0)
        return false;
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
