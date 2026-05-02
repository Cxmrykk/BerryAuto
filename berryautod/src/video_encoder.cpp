#include "video_encoder.hpp"
#include "globals.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

static void on_process(void* userdata)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(userdata);
    struct pw_buffer* b = pw_stream_dequeue_buffer(enc->pw_stream);
    if (!b)
        return;

    struct spa_buffer* buf = b->buffer;
    if (buf->datas[0].data)
    {
        int stride = buf->datas[0].chunk->stride;
        enc->process_pipewire_frame(buf->datas[0].data, stride, enc->get_desktop_width(), enc->get_desktop_height());
    }
    pw_stream_queue_buffer(enc->pw_stream, b);
}

// NEW: Track exactly what the PipeWire stream is doing
static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    if (error)
    {
        LOG_E("[PipeWire] Stream Error: " << error);
    }
    else
    {
        LOG_I("[PipeWire] Stream State changed to: " << pw_stream_state_as_string(state));
    }
}

static const struct pw_stream_events stream_events = []()
{
    struct pw_stream_events ev{};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.process = on_process;
    ev.state_changed = on_state_changed;
    return ev;
}();

VideoEncoder::VideoEncoder(int width, int height, NalCallback callback)
    : target_width(width), target_height(height), nal_callback(callback)
{
    pw_init(NULL, NULL);
}

VideoEncoder::~VideoEncoder()
{
    stop();
    pw_deinit();
}

void VideoEncoder::start()
{
    if (running.load())
        return;
    running = true;
    worker_thread = std::thread(&VideoEncoder::capture_loop, this);
}

void VideoEncoder::stop()
{
    if (!running.load())
        return;
    running = false;
    if (pw_loop)
        pw_main_loop_quit(pw_loop);
    if (worker_thread.joinable())
        worker_thread.join();
}

void VideoEncoder::force_keyframe()
{
    request_keyframe = true;
}

bool VideoEncoder::init_pipewire()
{
    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
        return false;

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core = pw_context_connect(pw_ctx, NULL, 0);
    if (!pw_core)
    {
        LOG_E("[PipeWire] Failed to connect to core. Is the PipeWire daemon running?");
        return false;
    }

    // Relaxed requirements to force auto-linking to the X11 screen module
    pw_stream = pw_stream_new(pw_core, "OpenGAL Capture",
                              pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                PW_KEY_NODE_NAME, "OpenGAL_Stream", NULL));

    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];

    struct spa_video_info_raw info;
    memset(&info, 0, sizeof(info));
    info.format = SPA_VIDEO_FORMAT_BGRx;
    info.size = SPA_RECTANGLE((uint32_t)target_width, (uint32_t)target_height);
    info.framerate = SPA_FRACTION(60, 1);

    params[0] = spa_format_video_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);

    if (res < 0)
    {
        LOG_E("[PipeWire] pw_stream_connect failed: " << spa_strerror(res));
        return false;
    }

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

bool VideoEncoder::init_encoder()
{
    std::vector<std::string> encoder_names;
    if (global_video_codec_type == 7)
        encoder_names = {"hevc_v4l2m2m", "libx265", "hevc"};
    else
        encoder_names = {"h264_v4l2m2m", "h264_omx", "libx264", "h264"};

    for (const auto& name : encoder_names)
    {
        codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
        {
            if (name == encoder_names.back())
                codec = avcodec_find_encoder(global_video_codec_type == 7 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
            if (!codec)
                continue;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        codec_ctx->width = target_width;
        codec_ctx->height = target_height;
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->time_base = {1, 60};
        codec_ctx->framerate = {60, 1};
        codec_ctx->gop_size = 60;
        codec_ctx->max_b_frames = 0;
        codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
        codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        codec_ctx->bit_rate = std::clamp(static_cast<int>(target_width * target_height * 60 * 0.12), 6000000, 16000000);

        if (std::string(codec->name) == "libx264" || std::string(codec->name) == "libx265")
        {
            av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
        }

        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
            break;
        avcodec_free_context(&codec_ctx);
        codec = nullptr;
    }

    if (!codec_ctx)
    {
        LOG_E("[FFmpeg] Failed to allocate encoder context.");
        return false;
    }

    frame = av_frame_alloc();
    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    av_frame_get_buffer(frame, 32);
    pkt = av_packet_alloc();

    sws_ctx = sws_getContext(target_width, target_height, AV_PIX_FMT_BGRA, target_width, target_height,
                             AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);

    return true;
}

void VideoEncoder::cleanup_encoder()
{
    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (frame)
        av_frame_free(&frame);
    if (pkt)
        av_packet_free(&pkt);
    if (codec_ctx)
        avcodec_free_context(&codec_ctx);
    sws_ctx = nullptr;
    frame = nullptr;
    pkt = nullptr;
    codec_ctx = nullptr;
}

void VideoEncoder::process_pipewire_frame(void* bgra_data, int stride, int /*pw_w*/, int pw_h)
{
    const uint8_t* in_data[1] = {(uint8_t*)bgra_data};
    int in_linesize[1] = {stride};

    sws_scale(sws_ctx, in_data, in_linesize, 0, pw_h, frame->data, frame->linesize);

    frame->pts = frame_pts++;
    frame->pict_type = request_keyframe.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    int ret = avcodec_send_frame(codec_ctx, frame);
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0)
            break;

        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

        std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
        nal_callback(nal_data, ts);
        av_packet_unref(pkt);
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_encoder() || !init_pipewire())
    {
        cleanup_encoder();
        cleanup_pipewire();
        return;
    }

    pw_main_loop_run(pw_loop);

    cleanup_pipewire();
    cleanup_encoder();
}
