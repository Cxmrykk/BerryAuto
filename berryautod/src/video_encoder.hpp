#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

extern "C"
{
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>
#include <sys/ipc.h>
#include <sys/shm.h>
}

#undef Status
#undef None
#undef Bool

using NalCallback = std::function<void(const std::vector<uint8_t>&, uint64_t)>;

class VideoEncoder
{
public:
    VideoEncoder(int width, int height, int fps, NalCallback callback);
    ~VideoEncoder();

    void start();
    void stop();
    void force_keyframe();

    void process_raw_frame(void* data, int stride, int w, int h);
    void update_sws();

    struct pw_stream* pw_stream = nullptr;
    int pw_w = 0;
    int pw_h = 0;
    AVPixelFormat pw_fmt = AV_PIX_FMT_BGRA;
    std::mutex sws_mutex;

    // --- Frame Caching for Wayland Damage Tracking ---
    std::vector<uint8_t> latest_frame_buffer;
    int latest_stride = 0;
    std::mutex frame_mutex;

private:
    int target_width, target_height, target_fps;
    NalCallback nal_callback;
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::atomic<bool> request_keyframe{false};

    // --- X11 Fallback ---
    Display* dpy = nullptr;
    Window root_window;
    XImage* img = nullptr;
    XShmSegmentInfo shminfo;
    bool init_x11();
    void cleanup_x11();

    // --- PipeWire ---
    struct pw_main_loop* pw_loop = nullptr;
    struct pw_context* pw_ctx = nullptr;
    struct pw_core* pw_core = nullptr;
    struct spa_hook stream_listener;
    bool init_pipewire(uint32_t node_id);
    void cleanup_pipewire();

    // --- FFmpeg ---
    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    bool init_encoder();
    void cleanup_encoder();

    void capture_loop();
};
