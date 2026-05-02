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
        enc->process_raw_frame(buf->datas[0].data, stride, enc->get_desktop_width(), enc->get_desktop_height());
    }
    pw_stream_queue_buffer(enc->pw_stream, b);
}

static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    (void)userdata;
    (void)old;
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

// --- X11 ENGINE ---
bool VideoEncoder::init_x11()
{
    const char* disp_env = getenv("DISPLAY");
    if (!disp_env)
        setenv("DISPLAY", ":0", 1);

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return false;

    root_window = DefaultRootWindow(dpy);
    img = XShmCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)), ZPixmap,
                          NULL, &shminfo, target_width, target_height);
    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    shminfo.shmaddr = img->data = (char*)shmat(shminfo.shmid, 0, 0);
    shminfo.readOnly = False;
    XShmAttach(dpy, &shminfo);
    return true;
}

void VideoEncoder::cleanup_x11()
{
    if (img)
    {
        XShmDetach(dpy, &shminfo);
        XSync(dpy, False);
        XDestroyImage(img);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, 0);
        img = nullptr;
    }
    if (dpy)
    {
        XCloseDisplay(dpy);
        dpy = nullptr;
    }
}

// --- WAYLAND/PIPEWIRE ENGINE ---
bool VideoEncoder::init_pipewire()
{
    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
        return false;

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core = pw_context_connect(pw_ctx, NULL, 0);
    if (!pw_core)
        return false;

    pw_stream = pw_stream_new(pw_core, "OpenGAL Capture",
                              pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                PW_KEY_MEDIA_ROLE, "Screen", PW_KEY_NODE_NAME, "OpenGAL_Stream", NULL));

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

// --- FFMPEG ENGINE ---
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

        // STRICT 60 FPS PACING
        codec_ctx->time_base = {1, 60};
        codec_ctx->framerate = {60, 1};
        codec_ctx->gop_size = 60;
        codec_ctx->max_b_frames = 0;

        // CRITICAL: Strictly enforce Baseline for Android Auto hardware compatibility
        codec_ctx->profile = FF_PROFILE_H264_BASELINE;

        // CRITICAL: Strict Constant Bitrate (CBR) to prevent Head Unit buffer overflows
        int target_bitrate = static_cast<int>(target_width * target_height * 60 * 0.08);
        target_bitrate = std::clamp(target_bitrate, 2000000, 6000000);

        codec_ctx->bit_rate = target_bitrate;
        codec_ctx->rc_min_rate = target_bitrate;
        codec_ctx->rc_max_rate = target_bitrate;
        codec_ctx->rc_buffer_size = target_bitrate; // 1-second VBV buffer constraint

        codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
        codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

        if (std::string(codec->name) == "libx264" || std::string(codec->name) == "libx265")
        {
            av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
            av_opt_set(codec_ctx->priv_data, "nal-hrd", "cbr", 0); // Strict CBR padding
        }

        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
            break;
        avcodec_free_context(&codec_ctx);
        codec = nullptr;
    }

    if (!codec_ctx)
        return false;

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

void VideoEncoder::process_raw_frame(void* bgra_data, int stride, int /*pw_w*/, int pw_h)
{
    const uint8_t* in_data[1] = {(uint8_t*)bgra_data};
    int in_linesize[1] = {stride};

    sws_scale(sws_ctx, in_data, in_linesize, 0, pw_h, frame->data, frame->linesize);

    // CRITICAL: Strictly align FFmpeg Presentation Time Stamp (PTS) to sequential frames
    frame->pts = frame_pts++;
    frame->pict_type = request_keyframe.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    int ret = avcodec_send_frame(codec_ctx, frame);
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0)
            break;

        // CRITICAL: Generate a perfectly spaced microseconds timestamp based on the strict PTS
        // 1,000,000 us / 60 fps = 16,666 us per frame.
        uint64_t exact_ts = frame->pts * 16666;

        std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
        nal_callback(nal_data, exact_ts);
        av_packet_unref(pkt);
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_encoder())
        return;

    if (getenv("WAYLAND_DISPLAY"))
    {
        LOG_I("[Capture] Wayland detected. Initializing PipeWire engine...");
        if (init_pipewire())
        {
            pw_main_loop_run(pw_loop);
            cleanup_pipewire();
        }
    }
    else
    {
        LOG_I("[Capture] X11 detected. Initializing XShm engine...");
        if (init_x11())
        {
            // STRICT CLOCK PACING (Guarantee exactly 60.00 frames per second)
            auto frame_duration = std::chrono::microseconds(1000000 / 60);
            auto next_frame_time = std::chrono::steady_clock::now();

            while (running.load())
            {
                XShmGetImage(dpy, root_window, img, 0, 0, AllPlanes);
                process_raw_frame((uint8_t*)img->data, img->bytes_per_line, img->width, img->height);

                next_frame_time += frame_duration;
                std::this_thread::sleep_until(next_frame_time);
            }
            cleanup_x11();
        }
    }

    cleanup_encoder();
}
