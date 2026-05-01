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
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
}

using NalCallback = std::function<void(const std::vector<uint8_t>&, uint64_t)>;

class VideoEncoder
{
public:
    VideoEncoder(int width, int height, NalCallback callback);
    ~VideoEncoder();

    void start();
    void stop();
    void force_keyframe();

    int get_desktop_width() const
    {
        return target_width;
    }
    int get_desktop_height() const
    {
        return target_height;
    }
    int get_scaled_w() const
    {
        return target_width;
    }
    int get_scaled_h() const
    {
        return target_height;
    }
    int get_offset_x() const
    {
        return 0;
    }
    int get_offset_y() const
    {
        return 0;
    }

    void process_pipewire_frame(void* data, int stride, int width, int height);

    // Make PipeWire stream public so the static C callback can access it
    struct pw_stream* pw_stream = nullptr;

private:
    int target_width, target_height;
    NalCallback nal_callback;
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::atomic<bool> request_keyframe{false};

    // PipeWire contexts
    struct pw_main_loop* pw_loop = nullptr;
    struct pw_context* pw_ctx = nullptr;
    struct pw_core* pw_core = nullptr;
    struct spa_hook stream_listener;

    // FFmpeg
    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    uint64_t frame_pts = 0;

    bool init_pipewire();
    void cleanup_pipewire();
    bool init_encoder();
    void cleanup_encoder();
    void capture_loop();
};
