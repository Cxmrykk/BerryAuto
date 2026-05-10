#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

extern "C"
{
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <wayland-client.h>
}

#undef Status
#undef None
#undef Bool

using NalCallback = std::function<void(const std::vector<uint8_t>&, uint64_t, bool)>;
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

    int pw_w = 0;
    int pw_h = 0;
    AVPixelFormat pw_fmt = AV_PIX_FMT_BGRA;
    std::mutex sws_mutex;

    std::vector<uint8_t> latest_frame_buffer;
    int latest_stride = 0;
    std::mutex frame_mutex;
    std::atomic<bool> running{false};

    bool has_wlr_screencopy = false;
    bool frame_ready = false;
    void* current_data = nullptr;
    size_t current_size = 0;
    wl_buffer* current_buffer = nullptr;
    wl_shm* wl_shm_inst = nullptr;
    wl_output* wl_out = nullptr;
    zwlr_screencopy_manager_v1* wlr_screencopy = nullptr;
    void request_wayland_frame_sync();

    pw_main_loop* pw_loop = nullptr;
    pw_context* pw_ctx = nullptr;
    pw_core* pw_core_inst = nullptr;
    pw_stream* pw_stream_inst = nullptr;
    spa_hook core_listener;
    spa_hook stream_listener;

private:
    int target_width, target_height, target_fps;
    NalCallback nal_callback;
    std::thread worker_thread;
    std::atomic<bool> request_keyframe{false};

    void capture_loop();

    void run_x11_loop();
    void run_wlr_loop();
    void run_pipewire_loop(uint32_t node_id, int pw_fd);

    Display* dpy = nullptr;
    Window root_window;
    XImage* img = nullptr;
    XShmSegmentInfo shminfo;
    bool init_x11();
    void cleanup_x11();

    wl_display* wl_dpy = nullptr;
    wl_registry* wl_reg = nullptr;
    bool init_wlr_registry();
    void cleanup_wlr_registry();

    bool init_pipewire(uint32_t node_id, int pw_fd);
    void cleanup_pipewire();

    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    bool init_encoder();
    void cleanup_encoder();
};
