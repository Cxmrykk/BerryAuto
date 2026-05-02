#include "video_encoder.hpp"
#include "globals.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <gio/gio.h>
#include <iostream>
#include <time.h>
#include <unistd.h>

static uint64_t get_monotonic_usec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

// -------------------------------------------------------------------------
// D-Bus XDG Desktop Portal Negotiator
// -------------------------------------------------------------------------
static uint32_t negotiated_node_id = 0;
static GMainLoop* dbus_loop = nullptr;

static void on_signal_response(GDBusConnection* conn, const gchar* sender, const gchar* path, const gchar* iface,
                               const gchar* signal, GVariant* params, gpointer user_data)
{
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    int step = GPOINTER_TO_INT(user_data);
    uint32_t response = 0;
    GVariant* results = nullptr;

    g_variant_get(params, "(u@a{sv})", &response, &results);

    if (response != 0)
    {
        LOG_E("[Portal] Request failed or cancelled (response=" << response
                                                                << "). Ensure chooser_type=none is set in config!");
        if (results)
            g_variant_unref(results);
        g_main_loop_quit(dbus_loop);
        return;
    }

    if (step == 3)
    {
        GVariant* streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (streams)
        {
            GVariantIter iter;
            g_variant_iter_init(&iter, streams);
            uint32_t node_id;
            GVariant* stream_props;
            if (g_variant_iter_next(&iter, "(u@a{sv})", &node_id, &stream_props))
            {
                negotiated_node_id = node_id;
                g_variant_unref(stream_props);
            }
            g_variant_unref(streams);
        }
    }

    if (results)
        g_variant_unref(results);
    g_main_loop_quit(dbus_loop);
}

static bool negotiate_wayland_screencast(uint32_t& out_node_id)
{
    GError* error = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn)
    {
        LOG_E("[Portal] Failed to connect to D-Bus Session: " << error->message);
        g_error_free(error);
        return false;
    }

    // Determine deterministic session and request paths based on our unique D-Bus name
    std::string sender = g_dbus_connection_get_unique_name(conn);
    sender.erase(std::remove(sender.begin(), sender.end(), ':'), sender.end());
    std::replace(sender.begin(), sender.end(), '.', '_');

    std::string session_path = "/org/freedesktop/portal/desktop/session/" + sender + "/berryauto";
    std::string request_path = "/org/freedesktop/portal/desktop/request/" + sender;

    dbus_loop = g_main_loop_new(nullptr, FALSE);

    // STEP 1: CreateSession
    guint sub1 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req1").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(1), nullptr);

    GVariantBuilder b1;
    g_variant_builder_init(&b1, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b1, "{sv}", "session_handle_token", g_variant_new_string("berryauto"));
    g_variant_builder_add(&b1, "{sv}", "handle_token", g_variant_new_string("req1"));

    GVariant* res1 = g_dbus_connection_call_sync(
        conn, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
        "CreateSession", g_variant_new("(a{sv})", &b1), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] CreateSession failed: " << error->message);
        return false;
    }
    g_variant_unref(res1);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub1);

    // STEP 2: SelectSources
    guint sub2 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req2").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(2), nullptr);

    GVariantBuilder b2;
    g_variant_builder_init(&b2, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b2, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b2, "{sv}", "types", g_variant_new_uint32(1)); // 1 = monitor
    g_variant_builder_add(&b2, "{sv}", "handle_token", g_variant_new_string("req2"));

    GVariant* res2 = g_dbus_connection_call_sync(conn, "org.freedesktop.portal.Desktop",
                                                 "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
                                                 "SelectSources", g_variant_new("(oa{sv})", session_path.c_str(), &b2),
                                                 nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] SelectSources failed: " << error->message);
        return false;
    }
    g_variant_unref(res2);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub2);

    // STEP 3: Start
    guint sub3 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req3").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(3), nullptr);

    GVariantBuilder b3;
    g_variant_builder_init(&b3, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b3, "{sv}", "handle_token", g_variant_new_string("req3"));

    GVariant* res3 = g_dbus_connection_call_sync(conn, "org.freedesktop.portal.Desktop",
                                                 "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast",
                                                 "Start", g_variant_new("(osa{sv})", session_path.c_str(), "", &b3),
                                                 nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (error)
    {
        LOG_E("[Portal] Start failed: " << error->message);
        return false;
    }
    g_variant_unref(res3);
    g_main_loop_run(dbus_loop);
    g_dbus_connection_signal_unsubscribe(conn, sub3);

    g_main_loop_unref(dbus_loop);
    g_object_unref(conn);

    if (negotiated_node_id > 0)
    {
        out_node_id = negotiated_node_id;
        return true;
    }
    return false;
}
// -------------------------------------------------------------------------

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

VideoEncoder::VideoEncoder(int width, int height, int fps, NalCallback callback)
    : target_width(width), target_height(height), target_fps(fps), nal_callback(callback)
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
bool VideoEncoder::init_pipewire(uint32_t node_id)
{
    pw_loop = pw_main_loop_new(NULL);
    if (!pw_loop)
    {
        LOG_E("[PipeWire] Failed to create main loop");
        return false;
    }

    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core = pw_context_connect(pw_ctx, NULL, 0);
    if (!pw_core)
    {
        LOG_E("[PipeWire] Failed to connect to core");
        return false;
    }

    pw_stream =
        pw_stream_new(pw_core, "OpenGAL Capture",
                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
                                        "Screen", PW_KEY_TARGET_OBJECT, std::to_string(node_id).c_str(), NULL));

    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];

    struct spa_video_info_raw info;
    memset(&info, 0, sizeof(info));
    info.format = SPA_VIDEO_FORMAT_BGRx;
    info.size = SPA_RECTANGLE((uint32_t)target_width, (uint32_t)target_height);
    info.framerate = SPA_FRACTION(target_fps, 1);

    params[0] = spa_format_video_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);

    if (res < 0)
    {
        LOG_E("[PipeWire] Failed to connect stream: " << spa_strerror(res));
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

        codec_ctx->colorspace = AVCOL_SPC_BT709;
        codec_ctx->color_range = AVCOL_RANGE_MPEG;
        codec_ctx->color_primaries = AVCOL_PRI_BT709;
        codec_ctx->color_trc = AVCOL_TRC_BT709;

        codec_ctx->time_base = {1, 1000000};
        codec_ctx->framerate = {target_fps, 1};
        codec_ctx->gop_size = target_fps * 2;
        codec_ctx->max_b_frames = 0;

        codec_ctx->profile = FF_PROFILE_H264_HIGH;

        int target_bitrate = static_cast<int>(target_width * target_height * target_fps * 0.15);
        target_bitrate = std::clamp(target_bitrate, 4000000, 40000000);

        codec_ctx->bit_rate = target_bitrate;
        codec_ctx->rc_min_rate = target_bitrate;
        codec_ctx->rc_max_rate = target_bitrate;
        codec_ctx->rc_buffer_size = target_bitrate / 2;

        codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
        codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

        if (std::string(codec->name) == "h264_v4l2m2m")
            av_opt_set(codec_ctx->priv_data, "profile", "high", 0);
        else if (std::string(codec->name) == "libx264" || std::string(codec->name) == "libx265")
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
        return false;

    frame = av_frame_alloc();
    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    av_frame_get_buffer(frame, 32);
    pkt = av_packet_alloc();

    sws_ctx = sws_getContext(target_width, target_height, AV_PIX_FMT_BGRA, target_width, target_height,
                             AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
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

    frame->pts = get_monotonic_usec();
    frame->pict_type = request_keyframe.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    int ret = avcodec_send_frame(codec_ctx, frame);
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0)
            break;

        uint64_t absolute_ts = pkt->pts;
        std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
        nal_callback(nal_data, absolute_ts);
        av_packet_unref(pkt);
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_encoder())
        return;

    if (getenv("WAYLAND_DISPLAY"))
    {
        LOG_I("[Capture] Wayland detected. Negotiating D-Bus ScreenCast Session...");
        uint32_t node_id = 0;

        if (negotiate_wayland_screencast(node_id))
        {
            LOG_I("[Capture] ScreenCast Negotiated successfully! Target Node ID: " << node_id);
            if (init_pipewire(node_id))
            {
                pw_main_loop_run(pw_loop);
                cleanup_pipewire();
            }
        }
        else
        {
            LOG_E("[Capture] Wayland ScreenCast negotiation failed. Did you configure xdg-desktop-portal-wlr?");
        }
    }
    else
    {
        LOG_I("[Capture] X11 detected. Initializing XShm engine...");
        if (init_x11())
        {
            uint64_t frame_interval_us = 1000000 / target_fps;
            uint64_t next_frame_time = get_monotonic_usec() + frame_interval_us;

            while (running.load())
            {
                XShmGetImage(dpy, root_window, img, 0, 0, AllPlanes);
                process_raw_frame((uint8_t*)img->data, img->bytes_per_line, img->width, img->height);

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
            cleanup_x11();
        }
    }

    cleanup_encoder();
}
