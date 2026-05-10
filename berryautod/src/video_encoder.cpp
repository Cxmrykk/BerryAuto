#include "video_encoder.hpp"
#include "dbus_portal.hpp"
#include "globals.hpp"
#include "input_handler.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libavcodec/avcodec.h>
#include <time.h>
#include <unistd.h>

uint64_t get_monotonic_usec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

VideoEncoder::VideoEncoder(int width, int height, int fps, NalCallback callback)
    : target_width(width), target_height(height), target_fps(fps), nal_callback(callback)
{
}

VideoEncoder::~VideoEncoder()
{
    stop();
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
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (pw_w == 0 || pw_h == 0)
        return;

    sws_ctx = sws_getContext(pw_w, pw_h, pw_fmt, target_width, target_height, AV_PIX_FMT_NV12, SWS_BILINEAR, NULL, NULL,
                             NULL);

    if (!sws_ctx)
        LOG_E("[Capture] CRITICAL: sws_getContext failed! Format: " << pw_fmt);
}

bool VideoEncoder::init_encoder()
{
    std::vector<std::string> encoder_names;

    // SOFTWARE ENCODING FIX:
    // We prioritize libx265 / libx264 over the hardware V4L2 encoders.
    // This forces the CPU to encode the stream, bypassing the buggy hardware H264 blocks.
    if (global_video_codec_type == 7)
        encoder_names = {"libx265", "hevc_v4l2m2m", "hevc"};
    else
        encoder_names = {"libx264", "h264_v4l2m2m", "h264_omx", "h264"};

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
        codec_ctx->pix_fmt = AV_PIX_FMT_NV12;
        codec_ctx->colorspace = AVCOL_SPC_BT709;
        codec_ctx->color_range = AVCOL_RANGE_MPEG;
        codec_ctx->color_primaries = AVCOL_PRI_BT709;
        codec_ctx->color_trc = AVCOL_TRC_BT709;
        codec_ctx->time_base = {1, 1000000};
        codec_ctx->framerate = {target_fps, 1};
        codec_ctx->gop_size = target_fps * 2;
        codec_ctx->max_b_frames = 0;
        codec_ctx->profile = FF_PROFILE_H264_BASELINE;

        int target_bitrate = static_cast<int>(target_width * target_height * target_fps * 0.15);
        target_bitrate = std::clamp(target_bitrate, 4000000, 40000000);
        codec_ctx->bit_rate = target_bitrate;
        codec_ctx->rc_min_rate = target_bitrate;
        codec_ctx->rc_max_rate = target_bitrate;
        codec_ctx->rc_buffer_size = target_bitrate / 2;
        codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
        codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

        if (std::string(codec->name) == "h264_v4l2m2m")
            av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
        else if (std::string(codec->name) == "libx264" || std::string(codec->name) == "libx265")
        {
            av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
        }

        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
        {
            LOG_I("[Capture] Successfully initialized encoder: " << codec->name);
            break;
        }
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
        avcodec_free_context(&codec_ctx);
    sws_ctx = nullptr;
    frame = nullptr;
    pkt = nullptr;
    codec_ctx = nullptr;
}

void VideoEncoder::process_raw_frame(void* raw_data, int stride, int pw_w, int pw_h)
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (!sws_ctx)
        return;

    const uint8_t* in_data[4] = {nullptr};
    int in_linesize[4] = {0};

    av_image_fill_arrays((uint8_t**)in_data, in_linesize, (const uint8_t*)raw_data, pw_fmt, pw_w, pw_h, 1);
    if (stride > in_linesize[0])
        in_linesize[0] = stride;

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
        bool is_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
        nal_callback(nal_data, absolute_ts, is_keyframe);
        av_packet_unref(pkt);
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_encoder())
        return;

    if (getenv("WAYLAND_DISPLAY"))
    {
        LOG_I("[Capture] Wayland detected. Probing compositor capabilities...");
        if (init_wlr_registry())
        {
            if (has_wlr_screencopy && wlr_screencopy && wl_out)
            {
                LOG_I("[Capture] wlroots compositor detected. Using native wlr-screencopy.");
                run_wlr_loop();
            }
            else
            {
                LOG_I("[Capture] GNOME/Mutter detected. Falling back to PipeWire Portal.");
                uint32_t node_id = 0;
                int pw_fd = -1;

                if (negotiate_wayland_screencast(node_id, pw_fd))
                {
                    run_pipewire_loop(node_id, pw_fd);
                }
                else
                {
                    LOG_E("[Capture] Wayland PipeWire Portal negotiation failed.");
                }
            }
            cleanup_wlr_registry();
        }
    }
    else
    {
        LOG_I("[Capture] X11 detected. Initializing XShm engine...");
        run_x11_loop();
    }

    cleanup_encoder();
}

void VideoEncoder::run_x11_loop()
{
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
