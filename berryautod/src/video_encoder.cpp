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

    // Interrupt PipeWire Loop
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
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (pw_w == 0 || pw_h == 0)
        return;

    // Use SWS_FAST_BILINEAR instead of SWS_BILINEAR for heavy SIMD optimizations.
    // MUST remain NV12: V4L2 M2M hardware encoders heavily rely on NV12 DMA stride compatibility
    sws_ctx = sws_getContext(pw_w, pw_h, pw_fmt, target_width, target_height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, NULL,
                             NULL, NULL);

    if (!sws_ctx)
        LOG_E("[Capture] CRITICAL: sws_getContext failed! Format: " << pw_fmt);
}

bool VideoEncoder::init_encoder()
{
    std::vector<std::string> encoder_names;

    if (!user_config_video_encoder.empty())
    {
        encoder_names.push_back(user_config_video_encoder);
    }
    else
    {
        if (global_video_codec_type == 7)
        {
            if (!user_config_disable_hw_encoding)
                encoder_names.push_back("hevc_v4l2m2m");
            encoder_names.push_back("libx265");
            encoder_names.push_back("hevc");
        }
        else
        {
            if (!user_config_disable_hw_encoding)
            {
                encoder_names.push_back("h264_v4l2m2m");
                encoder_names.push_back("h264_omx");
            }
            encoder_names.push_back("libx264");
            encoder_names.push_back("h264");
        }
    }

    for (const auto& name : encoder_names)
    {
        codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
        {
            if (name == encoder_names.back() && user_config_video_encoder.empty())
                codec = avcodec_find_encoder(global_video_codec_type == 7 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
            if (!codec)
                continue;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        codec_ctx->width = target_width;
        codec_ctx->height = target_height;

        // Strict requirement for Raspberry Pi hardware decoding memory alignment
        codec_ctx->pix_fmt = AV_PIX_FMT_NV12;

        codec_ctx->colorspace = AVCOL_SPC_BT709;
        codec_ctx->color_range = AVCOL_RANGE_MPEG;
        codec_ctx->color_primaries = AVCOL_PRI_BT709;
        codec_ctx->color_trc = AVCOL_TRC_BT709;
        codec_ctx->time_base = {1, 1000000};
        codec_ctx->framerate = {target_fps, 1};
        codec_ctx->gop_size = target_fps * 2;
        codec_ctx->max_b_frames = 0;

        int target_bitrate = static_cast<int>(target_width * target_height * target_fps * 0.15);
        if (user_config_video_bitrate > 0)
        {
            target_bitrate = user_config_video_bitrate;
        }
        target_bitrate = std::clamp(target_bitrate, 4000000, 40000000);
        codec_ctx->bit_rate = target_bitrate;

        bool is_hw = (name.find("v4l2m2m") != std::string::npos || name.find("omx") != std::string::npos);

        if (is_hw)
        {
            // Hardware Encoders
            // MUST be single-threaded. Generic Rate control parameters (qmin/LOW_DELAY) are omitted
            // as they confuse the V4L2 M2M driver, causing DMA buffer truncation & macroblock tearing.
            codec_ctx->thread_count = 1;
            codec_ctx->thread_type = 0;

            if (name == "h264_v4l2m2m")
            {
                av_opt_set(codec_ctx->priv_data, "profile", "main", 0);
            }
        }
        else
        {
            // Software Encoders
            codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
            codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

            codec_ctx->rc_max_rate = target_bitrate * 2;
            codec_ctx->rc_buffer_size = target_bitrate * 2;
            codec_ctx->qmin = 15;
            codec_ctx->qmax = 51;
            codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

            if (name == "libx264" || name == "libx265")
            {
                av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
                av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
            }
        }

        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
        {
            LOG_I("[Capture] Successfully initialized encoder: " << codec->name << " at " << target_bitrate << " bps");
            break;
        }
        avcodec_free_context(&codec_ctx);
        codec = nullptr;
    }

    if (!codec_ctx)
        return false;

    pkt = av_packet_alloc();

    return true;
}

void VideoEncoder::cleanup_encoder()
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (pkt)
        av_packet_free(&pkt);
    if (codec_ctx)
        avcodec_free_context(&codec_ctx);
    sws_ctx = nullptr;
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

    // ALLOCATE A FRESH FRAME FOR V4L2 M2M (HARDWARE DMA SAFETY)
    // Hardware encoders process asynchronously. Reusing a single persistent frame buffer
    // causes sws_scale to overwrite the pixels while the hardware is still reading them,
    // resulting in severe screen tearing. We create a new AVFrame per cycle.
    AVFrame* encode_frame = av_frame_alloc();
    encode_frame->format = codec_ctx->pix_fmt;
    encode_frame->width = codec_ctx->width;
    encode_frame->height = codec_ctx->height;

    // Align frame buffers to 32 bytes explicitly for V4L2 M2M stride compatibility
    av_frame_get_buffer(encode_frame, 32);

    sws_scale(sws_ctx, in_data, in_linesize, 0, pw_h, encode_frame->data, encode_frame->linesize);

    encode_frame->pts = get_monotonic_usec();

    bool keyframe_req = request_keyframe.exchange(false);
    encode_frame->pict_type = keyframe_req ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    encode_frame->key_frame = keyframe_req ? 1 : 0;

    int ret = avcodec_send_frame(codec_ctx, encode_frame);

    // Unref our handle. The encoder will keep its own internal reference if it's still
    // using it via DMA, releasing the memory only when it's entirely finished.
    av_frame_free(&encode_frame);

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
