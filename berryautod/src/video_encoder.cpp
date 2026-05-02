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

// Required for metadata negotiation
#include <spa/debug/types.h>
#include <spa/param/meta.h>
#include <spa/param/video/format-utils.h>

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

    std::string sender = g_dbus_connection_get_unique_name(conn);
    sender.erase(std::remove(sender.begin(), sender.end(), ':'), sender.end());
    std::replace(sender.begin(), sender.end(), '.', '_');

    std::string session_path = "/org/freedesktop/portal/desktop/session/" + sender + "/berryauto";
    std::string request_path = "/org/freedesktop/portal/desktop/request/" + sender;

    dbus_loop = g_main_loop_new(nullptr, FALSE);

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

    guint sub2 =
        g_dbus_connection_signal_subscribe(conn, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
                                           "Response", (request_path + "/req2").c_str(), nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, on_signal_response, GINT_TO_POINTER(2), nullptr);

    GVariantBuilder b2;
    g_variant_builder_init(&b2, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b2, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&b2, "{sv}", "types", g_variant_new_uint32(1));
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
        enc->process_raw_frame(buf->datas[0].data, stride, enc->pw_w, enc->pw_h);
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
        LOG_I("[PipeWire] Format negotiated! Size: " << info.size.width << "x" << info.size.height);
        enc->pw_w = info.size.width;
        enc->pw_h = info.size.height;

        switch (info.format)
        {
            case SPA_VIDEO_FORMAT_RGBx:
            case SPA_VIDEO_FORMAT_RGBA:
                enc->pw_fmt = AV_PIX_FMT_RGBA;
                break;
            case SPA_VIDEO_FORMAT_BGRx:
            case SPA_VIDEO_FORMAT_BGRA:
                enc->pw_fmt = AV_PIX_FMT_BGRA;
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
        enc->update_sws();
    }
}

static void on_state_changed(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
    (void)userdata;
    (void)old;
    if (error)
        LOG_E("[PipeWire] Stream Error: " << error);
    else
        LOG_I("[PipeWire] Stream State: " << pw_stream_state_as_string(state));
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

void VideoEncoder::update_sws()
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (pw_w == 0 || pw_h == 0)
        return;
    sws_ctx = sws_getContext(pw_w, pw_h, pw_fmt, target_width, target_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL,
                             NULL, NULL);
}

bool VideoEncoder::init_x11()
{
    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return false;
    root_window = DefaultRootWindow(dpy);
    img = XShmCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)), ZPixmap,
                          NULL, &shminfo, target_width, target_height);
    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    shminfo.shmaddr = img->data = (char*)shmat(shminfo.shmid, 0, 0);
    XShmAttach(dpy, &shminfo);
    pw_w = target_width;
    pw_h = target_height;
    pw_fmt = AV_PIX_FMT_BGRA;
    update_sws();
    return true;
}

void VideoEncoder::cleanup_x11()
{
    if (img)
    {
        XShmDetach(dpy, &shminfo);
        XDestroyImage(img);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, 0);
    }
    if (dpy)
        XCloseDisplay(dpy);
}

bool VideoEncoder::init_pipewire(uint32_t node_id)
{
    pw_loop = pw_main_loop_new(NULL);
    pw_ctx = pw_context_new(pw_main_loop_get_loop(pw_loop), NULL, 0);
    pw_core = pw_context_connect(pw_ctx, NULL, 0);
    if (!pw_core)
        return false;

    pw_stream =
        pw_stream_new(pw_core, "OpenGAL Capture",
                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
                                        "Screen", PW_KEY_TARGET_OBJECT, std::to_string(node_id).c_str(), NULL));

    pw_stream_add_listener(pw_stream, &stream_listener, &stream_events, this);

    uint8_t buffer[2048];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[2];

    // 1. Format Choice with Size Range (Strict Requirement for some Portals)
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx,
                               SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&SPA_RECTANGLE(target_width, target_height), &SPA_RECTANGLE(320, 240),
                                       &SPA_RECTANGLE(3840, 2160)));

    // 2. Negotiate Metadata Header (CRITICAL FIX)
    params[1] = (const struct spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));

    int res = pw_stream_connect(pw_stream, PW_DIRECTION_INPUT, node_id,
                                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 2);

    return (res >= 0);
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
}

bool VideoEncoder::init_encoder()
{
    av_log_set_level(AV_LOG_ERROR);
    std::vector<std::string> encoder_names;
    if (global_video_codec_type == 7)
        encoder_names = {"hevc_v4l2m2m", "libx265"};
    else
        encoder_names = {"h264_v4l2m2m", "h264_omx", "libx264"};

    for (const auto& name : encoder_names)
    {
        codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
            continue;
        codec_ctx = avcodec_alloc_context3(codec);
        codec_ctx->width = target_width;
        codec_ctx->height = target_height;
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->time_base = {1, 1000000};
        codec_ctx->framerate = {target_fps, 1};
        codec_ctx->gop_size = target_fps * 2;
        codec_ctx->max_b_frames = 0;
        int target_bitrate = std::clamp((int)(target_width * target_height * target_fps * 0.15), 4000000, 40000000);
        codec_ctx->bit_rate = target_bitrate;
        if (name.find("v4l2") != std::string::npos)
            av_opt_set(codec_ctx->priv_data, "profile", "high", 0);
        else
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
    return true;
}

void VideoEncoder::cleanup_encoder()
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (frame)
        av_frame_free(&frame);
    if (pkt)
        av_packet_free(&pkt);
    if (codec_ctx)
        av_codec_free_context(&codec_ctx);
}

void VideoEncoder::process_raw_frame(void* bgra_data, int stride, int pw_w, int pw_h)
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (!sws_ctx)
        return;
    const uint8_t* in_data[1] = {(uint8_t*)bgra_data};
    int in_linesize[1] = {stride};
    sws_scale(sws_ctx, in_data, in_linesize, 0, pw_h, frame->data, frame->linesize);
    frame->pts = get_monotonic_usec();
    frame->pict_type = request_keyframe.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (avcodec_send_frame(codec_ctx, frame) >= 0)
    {
        while (avcodec_receive_packet(codec_ctx, pkt) >= 0)
        {
            std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
            nal_callback(nal_data, pkt->pts);
            av_packet_unref(pkt);
        }
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_encoder())
        return;
    if (getenv("WAYLAND_DISPLAY"))
    {
        uint32_t node_id = 0;
        if (negotiate_wayland_screencast(node_id))
        {
            if (init_pipewire(node_id))
            {
                pw_main_loop_run(pw_loop);
                cleanup_pipewire();
            }
        }
    }
    else
    {
        if (init_x11())
        {
            uint64_t interval = 1000000 / target_fps;
            while (running.load())
            {
                XShmGetImage(dpy, root_window, img, 0, 0, AllPlanes);
                process_raw_frame((uint8_t*)img->data, img->bytes_per_line, img->width, img->height);
                usleep(interval);
            }
            cleanup_x11();
        }
    }
    cleanup_encoder();
}
