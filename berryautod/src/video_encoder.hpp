#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

extern "C"
{
#include <evdi_lib.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

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

    int input_w = 0;
    int input_h = 0;
    AVPixelFormat input_fmt = AV_PIX_FMT_BGRA;
    AVPixelFormat encoder_pix_fmt = AV_PIX_FMT_NV12;
    std::mutex sws_mutex;

    // Zero-allocation double buffering system
    std::vector<uint8_t> frame_buffers[2];
    int write_idx = 0;
    int read_idx = 1;

    int latest_stride = 0;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    bool frame_ready{false};

    std::atomic<bool> running{false};

    evdi_handle evdi = EVDI_INVALID_HANDLE;
    evdi_event_context evdi_ctx;
    struct evdi_buffer* evdi_buffers = nullptr;
    int evdi_buffer_count = 2;
    void handle_evdi_update(int buffer_id);

private:
    int target_width, target_height, target_fps;
    NalCallback nal_callback;
    std::thread worker_thread;
    std::atomic<bool> request_keyframe{false};

    void capture_loop();
    void run_evdi_loop();

    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    bool init_encoder();
    void cleanup_encoder();

    std::vector<uint8_t> get_edid(int width, int height);
};
