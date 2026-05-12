#include "video_encoder.hpp"
#include "globals.hpp"
#include "input_handler.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libavcodec/avcodec.h>
#include <string>
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

    frame_cv.notify_one();

    if (worker_thread.joinable())
        worker_thread.join();
}

void VideoEncoder::force_keyframe()
{
    request_keyframe = true;
    frame_cv.notify_one();
}

void VideoEncoder::update_sws()
{
    std::lock_guard<std::mutex> lock(sws_mutex);
    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (input_w == 0 || input_h == 0)
        return;

    // Use SWS_POINT because we expect 1:1 scaling (just color space conversion to NV12)
    sws_ctx = sws_getContext(input_w, input_h, input_fmt, target_width, target_height, encoder_pix_fmt, SWS_POINT, NULL,
                             NULL, NULL);

    if (!sws_ctx)
        LOG_E("[Capture] CRITICAL: sws_getContext failed! Format: " << input_fmt);
}

bool VideoEncoder::init_encoder()
{
    std::vector<const AVCodec*> candidates;
    AVCodecID target_id = (global_video_codec_type == 7) ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;

    if (!user_config_video_encoder.empty())
    {
        const AVCodec* c = avcodec_find_encoder_by_name(user_config_video_encoder.c_str());
        if (c)
            candidates.push_back(c);
        else
            LOG_E("[Capture] User-specified encoder '" << user_config_video_encoder << "' not found! Falling back.");
    }

    if (candidates.empty())
    {
        std::vector<const AVCodec*> hw_encoders;
        std::vector<const AVCodec*> sw_encoders;
        std::vector<std::string> hw_keywords = {"v4l2m2m", "omx", "vaapi", "nvenc", "qsv", "amf", "rpi"};

        void* opaque = nullptr;
        const AVCodec* c = nullptr;
        while ((c = av_codec_iterate(&opaque)) != nullptr)
        {
            if (av_codec_is_encoder(c) && c->id == target_id)
            {
                std::string name = c->name;
                bool is_hw = false;
                for (const auto& kw : hw_keywords)
                {
                    if (name.find(kw) != std::string::npos)
                    {
                        is_hw = true;
                        break;
                    }
                }
                if (is_hw)
                    hw_encoders.push_back(c);
                else
                    sw_encoders.push_back(c);
            }
        }

        if (!user_config_disable_hw_encoding)
        {
            for (auto hw_c : hw_encoders)
            {
                std::string name = hw_c->name;
                if (name.find("v4l2m2m") != std::string::npos || name.find("omx") != std::string::npos ||
                    name.find("rpi") != std::string::npos)
                {
                    candidates.push_back(hw_c);
                }
            }
            for (auto hw_c : hw_encoders)
            {
                if (std::find(candidates.begin(), candidates.end(), hw_c) == candidates.end())
                    candidates.push_back(hw_c);
            }
        }

        const AVCodec* libx = avcodec_find_encoder_by_name(target_id == AV_CODEC_ID_HEVC ? "libx265" : "libx264");
        if (libx)
            candidates.push_back(libx);

        for (auto sw_c : sw_encoders)
        {
            if (sw_c != libx && std::find(candidates.begin(), candidates.end(), sw_c) == candidates.end())
                candidates.push_back(sw_c);
        }
    }

    for (const AVCodec* c : candidates)
    {
        codec = c;
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx)
            continue;

        encoder_pix_fmt = AV_PIX_FMT_NV12;
        if (codec->pix_fmts)
        {
            bool found_nv12 = false;
            const enum AVPixelFormat* p = codec->pix_fmts;
            while (*p != AV_PIX_FMT_NONE)
            {
                if (*p == AV_PIX_FMT_NV12)
                    found_nv12 = true;
                p++;
            }
            if (!found_nv12)
                encoder_pix_fmt = codec->pix_fmts[0];
        }

        codec_ctx->width = target_width;
        codec_ctx->height = target_height;
        codec_ctx->pix_fmt = encoder_pix_fmt;
        codec_ctx->colorspace = AVCOL_SPC_BT709;
        codec_ctx->color_range = AVCOL_RANGE_MPEG;
        codec_ctx->color_primaries = AVCOL_PRI_BT709;
        codec_ctx->color_trc = AVCOL_TRC_BT709;
        codec_ctx->time_base = {1, 1000000};
        codec_ctx->framerate = {target_fps, 1};
        codec_ctx->gop_size = target_fps * 2;
        codec_ctx->max_b_frames = 0;

        codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

        int target_bitrate = static_cast<int>(target_width * target_height * target_fps * 0.15);
        if (user_config_video_bitrate > 0)
            target_bitrate = user_config_video_bitrate;
        target_bitrate = std::clamp(target_bitrate, 4000000, 40000000);

        codec_ctx->bit_rate = target_bitrate;
        codec_ctx->rc_max_rate = target_bitrate * 1.5;
        codec_ctx->rc_buffer_size = target_bitrate / 2;

        std::string name = codec->name;
        bool is_hw = false;
        std::vector<std::string> hw_keywords = {"v4l2m2m", "omx", "vaapi", "nvenc", "qsv", "amf", "rpi"};
        for (const auto& kw : hw_keywords)
        {
            if (name.find(kw) != std::string::npos)
            {
                is_hw = true;
                break;
            }
        }

        if (is_hw)
        {
            codec_ctx->thread_count = 1;
            codec_ctx->thread_type = 0;
            av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
        }
        else
        {
            codec_ctx->thread_count = std::max(1u, std::thread::hardware_concurrency());
            codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            codec_ctx->qmin = 10;
            codec_ctx->qmax = 35;
        }

        if (!user_config_video_profile.empty())
        {
            av_opt_set(codec_ctx->priv_data, "profile", user_config_video_profile.c_str(), 0);
        }
        else if (is_hw && name.find("v4l2m2m") != std::string::npos)
        {
            av_opt_set(codec_ctx->priv_data, "profile", "main", 0);
            av_opt_set(codec_ctx->priv_data, "num_capture_buffers", "4", 0);
        }

        if (!is_hw || name.find("libx") != std::string::npos)
        {
            std::string preset = user_config_video_preset.empty() ? "superfast" : user_config_video_preset;
            std::string tune = user_config_video_tune.empty() ? "zerolatency" : user_config_video_tune;
            av_opt_set(codec_ctx->priv_data, "preset", preset.c_str(), 0);
            av_opt_set(codec_ctx->priv_data, "tune", tune.c_str(), 0);
        }

        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
        {
            LOG_I("[Capture] Successfully initialized encoder: " << codec->name << " at " << target_bitrate << " bps");
            break;
        }

        LOG_I("[Capture] Failed to initialize encoder: " << codec->name << ". Trying the next available candidate...");
        avcodec_free_context(&codec_ctx);
        codec = nullptr;
    }

    if (!codec_ctx)
    {
        LOG_E("[Capture] CRITICAL: Failed to initialize ANY available video encoders on this system!");
        return false;
    }

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

    av_image_fill_arrays((uint8_t**)in_data, in_linesize, (const uint8_t*)raw_data, input_fmt, pw_w, pw_h, 1);
    if (stride > in_linesize[0])
        in_linesize[0] = stride;

    AVFrame* encode_frame = av_frame_alloc();
    encode_frame->format = codec_ctx->pix_fmt;
    encode_frame->width = codec_ctx->width;
    encode_frame->height = codec_ctx->height;
    av_frame_get_buffer(encode_frame, 32);

    sws_scale(sws_ctx, in_data, in_linesize, 0, pw_h, encode_frame->data, encode_frame->linesize);

    encode_frame->pts = get_monotonic_usec();

    bool keyframe_req = request_keyframe.exchange(false);

    // Setting pict_type to I forces libavcodec to generate an I-Frame natively.
    // Setting the deprecated 'key_frame' boolean is redundant and triggers compiler warnings.
    encode_frame->pict_type = keyframe_req ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    int ret = avcodec_send_frame(codec_ctx, encode_frame);
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

    LOG_I("[Capture] Initializing EVDI Kernel Virtual Monitor...");
    run_evdi_loop();

    cleanup_encoder();
}
