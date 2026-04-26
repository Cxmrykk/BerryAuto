#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>
#include "transport_ffs.h"
#include "crypto_tls.h"

class VideoEncoderThread {
public:
    VideoEncoderThread(FunctionFSTransport& transport, OpenGALTlsContext& tls, int channel_id);
    ~VideoEncoderThread();
    void start(int width, int height);
    void stop();
private:
    void encode_loop();
    bool init_drm_capture();
    bool init_v4l2_encoder();
    void cleanup_hardware();
    void generate_test_pattern();

    FunctionFSTransport& usb_transport;
    OpenGALTlsContext& tls_ctx;
    int ch_id;
    std::thread worker;
    std::atomic<bool> running;

    bool use_test_pattern = false;
    int frame_counter = 0;

    int drm_fd = -1;
    uint32_t fb_id = 0;
    uint8_t* drm_mapped_buffer = nullptr;
    size_t drm_buffer_size = 0;
    int res_w = 800; int res_h = 480;

    int v4l2_fd = -1;
    uint8_t* v4l2_in_buffer = nullptr;
    uint8_t* v4l2_out_buffer = nullptr;
    size_t v4l2_in_len = 0; size_t v4l2_out_len = 0;
};