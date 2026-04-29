#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <sys/ipc.h>
#include <sys/shm.h>
}

#include "x11_wrapper.hpp"

using NalCallback = std::function<void(const std::vector<uint8_t>&, uint64_t)>;

class VideoEncoder
{
public:
    VideoEncoder(int width, int height, NalCallback callback);
    ~VideoEncoder();

    void start();
    void stop();
    void force_keyframe();

    int get_desktop_width() const;
    int get_desktop_height() const;
    int get_scaled_w() const;
    int get_scaled_h() const;
    int get_offset_x() const;
    int get_offset_y() const;

private:
    int target_width, target_height;
    int scaled_w = 0, scaled_h = 0;
    int offset_x = 0, offset_y = 0;

    NalCallback nal_callback;
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::atomic<bool> request_keyframe{false};

    Display* dpy = nullptr;
    Window root_window;
    XImage* img = nullptr;
    XShmSegmentInfo shminfo;
    int capture_x = 0;
    int capture_y = 0;
    int capture_w = 0;
    int capture_h = 0;

    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    uint64_t frame_pts = 0;

    bool init_x11();
    void cleanup_x11();
    bool init_encoder();
    void cleanup_encoder();
    void encode_frame(uint8_t* bgra_data, int stride);
    void capture_loop();
};