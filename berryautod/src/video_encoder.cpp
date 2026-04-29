#include "video_encoder.hpp"
#include "globals.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

VideoEncoder::VideoEncoder(int width, int height, NalCallback callback)
    : target_width(width), target_height(height), nal_callback(callback)
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
    {
        worker_thread.join();
    }
}

void VideoEncoder::force_keyframe()
{
    request_keyframe = true;
}

int VideoEncoder::get_desktop_width() const
{
    return capture_w;
}
int VideoEncoder::get_desktop_height() const
{
    return capture_h;
}
int VideoEncoder::get_scaled_w() const
{
    return scaled_w;
}
int VideoEncoder::get_scaled_h() const
{
    return scaled_h;
}
int VideoEncoder::get_offset_x() const
{
    return offset_x;
}
int VideoEncoder::get_offset_y() const
{
    return offset_y;
}

bool VideoEncoder::init_x11()
{
    const char* disp_env = getenv("DISPLAY");
    if (!disp_env)
    {
        setenv("DISPLAY", ":0", 1);
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        std::cerr << "[VideoEncoder] FATAL: Failed to open X11 Display." << std::endl;
        return false;
    }
    root_window = DefaultRootWindow(dpy);

    int rr_event_base, rr_error_base;
    if (XRRQueryExtension(dpy, &rr_event_base, &rr_error_base))
    {
        XRRScreenResources* res = XRRGetScreenResources(dpy, root_window);
        if (res)
        {
            for (int i = 0; i < res->noutput; ++i)
            {
                XRROutputInfo* output_info = XRRGetOutputInfo(dpy, res, res->outputs[i]);
                if (output_info && output_info->connection == RR_Connected && output_info->crtc)
                {
                    std::string name(output_info->name);
                    if (name.find("Virtual") != std::string::npos || name.find("VGA") != std::string::npos)
                    {
                        XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);
                        if (crtc_info)
                        {
                            capture_x = crtc_info->x;
                            capture_y = crtc_info->y;
                            capture_w = crtc_info->width;
                            capture_h = crtc_info->height;
                            std::cout << "[VideoEncoder] Found Virtual Display '" << name
                                      << "' at Offset: " << capture_x << "," << capture_y << " | Size: " << capture_w
                                      << "x" << capture_h << std::endl;
                            XRRFreeCrtcInfo(crtc_info);
                            XRRFreeOutputInfo(output_info);
                            break;
                        }
                    }
                }
                if (output_info)
                    XRRFreeOutputInfo(output_info);
            }
            XRRFreeScreenResources(res);
        }
    }

    if (capture_w == 0 || capture_h == 0)
    {
        XWindowAttributes attributes;
        XGetWindowAttributes(dpy, root_window, &attributes);
        capture_w = attributes.width;
        capture_h = attributes.height;
        capture_x = 0;
        capture_y = 0;
        std::cout << "[VideoEncoder] Capturing primary monitor at 0,0 Size: " << capture_w << "x" << capture_h
                  << std::endl;
    }

    img = XShmCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)), ZPixmap,
                          NULL, &shminfo, capture_w, capture_h);
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

bool VideoEncoder::init_encoder()
{
    std::vector<std::string> encoder_names;

    if (global_video_codec_type == 7) // MEDIA_CODEC_VIDEO_H265
    {
        std::cout << "[VideoEncoder] Head Unit negotiated HEVC (H.265)." << std::endl;
        encoder_names = {"hevc_v4l2m2m", "libx265", "hevc"};
    }
    else // Default H.264
    {
        std::cout << "[VideoEncoder] Head Unit negotiated H.264." << std::endl;
        encoder_names = {"h264_v4l2m2m", "h264_omx", "libx264", "h264"};
    }

    // Try encoders sequentially, actually opening them to ensure hardware support exists
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
        codec_ctx->time_base = {1, 30};
        codec_ctx->framerate = {30, 1};
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->gop_size = 30;
        codec_ctx->max_b_frames = 0;
        codec_ctx->bit_rate = 6000000;

        if (std::string(codec->name) == "libx264" || std::string(codec->name) == "libx265")
        {
            av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
            if (std::string(codec->name) == "libx264")
                av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
        }
        else if (std::string(codec->name) == "h264_v4l2m2m" || std::string(codec->name) == "hevc_v4l2m2m")
        {
            // Restrict V4L2 DMA buffers to prevent CMA No Space crashes
            av_opt_set(codec_ctx->priv_data, "num_capture_buffers", "16", 0);
            av_opt_set(codec_ctx->priv_data, "num_output_buffers", "16", 0);
        }

        std::cout << "[VideoEncoder] Attempting to open Encoder: " << codec->name << "..." << std::endl;
        if (avcodec_open2(codec_ctx, codec, NULL) >= 0)
        {
            std::cout << "[VideoEncoder] Successfully opened Encoder: " << codec->name << std::endl;
            break; // Success! It was found AND the hardware accepted it.
        }
        else
        {
            std::cerr << "[VideoEncoder] Failed to open Encoder: " << codec->name << ", trying fallback..."
                      << std::endl;
            avcodec_free_context(&codec_ctx);
            codec = nullptr;
        }
    }

    if (!codec_ctx || !codec)
    {
        std::cerr << "[VideoEncoder] FATAL: Could not open any working video encoders for the requested codec."
                  << std::endl;
        return false;
    }

    int usable_w = std::max(2, target_width - global_video_margin_w);
    int usable_h = std::max(2, target_height - global_video_margin_h);

    scaled_w = usable_w;
    scaled_h = usable_h;
    offset_x = global_video_margin_w / 2;
    offset_y = global_video_margin_h / 2;

    scaled_w &= ~1;
    scaled_h &= ~1;
    offset_x &= ~1;
    offset_y &= ~1;

    std::cout << "[VideoEncoder] Stretching Capture (" << capture_w << "x" << capture_h
              << ") to fill Usable Area: " << scaled_w << "x" << scaled_h << " (Offset X:" << offset_x
              << " Y:" << offset_y << ")" << std::endl;

    frame = av_frame_alloc();
    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    av_frame_get_buffer(frame, 32);

    pkt = av_packet_alloc();

    sws_ctx = sws_getContext(capture_w, capture_h, AV_PIX_FMT_BGRA, scaled_w, scaled_h, AV_PIX_FMT_YUV420P,
                             SWS_FAST_BILINEAR, NULL, NULL, NULL);

    return true;
}

void VideoEncoder::cleanup_encoder()
{
    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (frame)
    {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (pkt)
    {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
    if (codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
}

void VideoEncoder::encode_frame(uint8_t* bgra_data, int stride)
{
    for (int y = 0; y < codec_ctx->height; ++y)
        memset(frame->data[0] + y * frame->linesize[0], 0, codec_ctx->width);
    for (int y = 0; y < codec_ctx->height / 2; ++y)
    {
        memset(frame->data[1] + y * frame->linesize[1], 128, codec_ctx->width / 2);
        memset(frame->data[2] + y * frame->linesize[2], 128, codec_ctx->width / 2);
    }

    const uint8_t* in_data[1] = {bgra_data};
    int in_linesize[1] = {stride};

    uint8_t* dst_data[4] = {frame->data[0] + offset_y * frame->linesize[0] + offset_x,
                            frame->data[1] + (offset_y / 2) * frame->linesize[1] + (offset_x / 2),
                            frame->data[2] + (offset_y / 2) * frame->linesize[2] + (offset_x / 2), NULL};
    int dst_linesize[4] = {frame->linesize[0], frame->linesize[1], frame->linesize[2], 0};

    sws_scale(sws_ctx, in_data, in_linesize, 0, capture_h, dst_data, dst_linesize);

    frame->pts = frame_pts++;

    if (request_keyframe.exchange(false))
    {
        frame->pict_type = AV_PICTURE_TYPE_I;
    }
    else
    {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
    }

    int ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0)
    {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[VideoEncoder] avcodec_send_frame failed: " << errbuf << std::endl;
        return;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
        {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[VideoEncoder] avcodec_receive_packet failed: " << errbuf << std::endl;
            break;
        }

        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

        std::vector<uint8_t> nal_data(pkt->data, pkt->data + pkt->size);
        nal_callback(nal_data, ts);

        av_packet_unref(pkt);
    }
}

void VideoEncoder::capture_loop()
{
    if (!init_x11() || !init_encoder())
    {
        std::cerr << "[VideoEncoder] Initialization failed" << std::endl;
        cleanup_x11();
        cleanup_encoder();
        return;
    }

    auto frame_duration = std::chrono::microseconds(1000000 / 30);

    while (running.load())
    {
        auto start_time = std::chrono::steady_clock::now();

        XShmGetImage(dpy, root_window, img, capture_x, capture_y, AllPlanes);

        encode_frame((uint8_t*)img->data, img->bytes_per_line);

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        if (elapsed < frame_duration)
        {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    cleanup_x11();
    cleanup_encoder();
}
