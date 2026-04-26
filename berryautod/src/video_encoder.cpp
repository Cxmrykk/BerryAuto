#include "video_encoder.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <algorithm>
#include <cerrno>

#define IOCTL_OR_FAIL(fd, req, arg, msg) \
    if (ioctl(fd, req, arg) < 0) { std::cerr << "[V4L2-ERR] " << msg << " failed: " << strerror(errno) << "\n"; return false; }

VideoEncoderThread::VideoEncoderThread(FunctionFSTransport& transport, OpenGALTlsContext& tls, int channel_id)
    : usb_transport(transport), tls_ctx(tls), ch_id(channel_id), running(false) {}

VideoEncoderThread::~VideoEncoderThread() { stop(); cleanup_hardware(); }

void VideoEncoderThread::start(int width, int height) {
    res_w = width; res_h = height;
    std::cout << "[HW-DEBUG] Attempting to start hardware pipeline (" << width << "x" << height << ")" << std::endl;

    if (!init_drm_capture()) {
        std::cout << "[HW-WARN] No active Linux Desktop found (Headless Mode?). Falling back to internal Test Pattern Generator." << std::endl;
        use_test_pattern = true;
    } 

    if (!init_v4l2_encoder()) {
        std::cerr << "[HW-ERR] V4L2 Hardware Encoder Initialization Failed. Aborting video stream." << std::endl;
        return;
    } 

    running = true;
    worker = std::thread(&VideoEncoderThread::encode_loop, this);
}

void VideoEncoderThread::stop() {
    running = false;
    if (worker.joinable()) worker.join();
}

bool VideoEncoderThread::init_drm_capture() {
    for (int i = 0; i < 3; i++) {
        std::string path = "/dev/dri/card" + std::to_string(i);
        drm_fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (drm_fd < 0) continue;

        drmModeRes* res = drmModeGetResources(drm_fd);
        if (!res || res->count_crtcs == 0) {
            if (res) drmModeFreeResources(res);
            close(drm_fd);
            continue;
        }

        for (int c = 0; c < res->count_crtcs; c++) {
            drmModeCrtc* crtc = drmModeGetCrtc(drm_fd, res->crtcs[c]);
            if (crtc && crtc->buffer_id) {
                fb_id = crtc->buffer_id;
                drmModeFB* fb = drmModeGetFB(drm_fd, fb_id);
                if (fb) {
                    std::cout << "[DRM-DEBUG] Found active framebuffer on " << path 
                              << " (Res: " << fb->width << "x" << fb->height << ")" << std::endl;
                    drm_buffer_size = fb->pitch * fb->height;
                    struct drm_mode_map_dumb map_req = {}; 
                    map_req.handle = fb->handle;
                    
                    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) == 0) {
                        drm_mapped_buffer = (uint8_t*)mmap(0, drm_buffer_size, PROT_READ, MAP_SHARED, drm_fd, map_req.offset);
                        if (drm_mapped_buffer != MAP_FAILED) {
                            drmModeFreeFB(fb); drmModeFreeCrtc(crtc); drmModeFreeResources(res);
                            return true;
                        }
                    }
                    drmModeFreeFB(fb);
                }
            }
            if (crtc) drmModeFreeCrtc(crtc);
        }
        drmModeFreeResources(res);
        close(drm_fd);
    }
    return false;
}

bool VideoEncoderThread::init_v4l2_encoder() {
    v4l2_fd = open("/dev/video11", O_RDWR | O_NONBLOCK);
    if (v4l2_fd < 0) {
        v4l2_fd = open("/dev/video12", O_RDWR | O_NONBLOCK);
        if (v4l2_fd < 0) return false;
    }

    struct v4l2_format fmt_out = {};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt_out.fmt.pix.width = res_w; fmt_out.fmt.pix.height = res_h;
    // FIX: Raspberry Pi encoder ONLY accepts YUV420!
    fmt_out.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_out, "Set output format (YUV420)");

    struct v4l2_format fmt_cap = {};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt_cap.fmt.pix.width = res_w; fmt_cap.fmt.pix.height = res_h;
    fmt_cap.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_cap, "Set capture format (H264)");

    struct v4l2_ext_control ctrls[3] = {};
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE; ctrls[0].value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    ctrls[1].id = V4L2_CID_MPEG_VIDEO_BITRATE; ctrls[1].value = 4000000;
    ctrls[2].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD; ctrls[2].value = 30;
    
    struct v4l2_ext_controls ext_ctrls = {};
    ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG; ext_ctrls.count = 3; ext_ctrls.controls = ctrls;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls, "Set Encoder Controls");

    struct v4l2_requestbuffers req_out = {};
    req_out.count = 1; req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT; req_out.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_out, "Request Out Buffers");

    struct v4l2_requestbuffers req_cap = {};
    req_cap.count = 1; req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req_cap.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_cap, "Request Cap Buffers");

    struct v4l2_buffer buf_out = {};
    buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT; buf_out.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QUERYBUF, &buf_out, "Query Out Buffer");
    v4l2_in_len = buf_out.length;
    v4l2_in_buffer = (uint8_t*)mmap(NULL, buf_out.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_out.m.offset);

    struct v4l2_buffer buf_cap = {};
    buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf_cap.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QUERYBUF, &buf_cap, "Query Cap Buffer");
    v4l2_out_len = buf_cap.length;
    v4l2_out_buffer = (uint8_t*)mmap(NULL, buf_cap.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_cap.m.offset);

    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT; IOCTL_OR_FAIL(v4l2_fd, VIDIOC_STREAMON, &type, "StreamOn Output");
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE; IOCTL_OR_FAIL(v4l2_fd, VIDIOC_STREAMON, &type, "StreamOn Capture");
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QBUF, &buf_cap, "Queue Capture Buffer");

    std::cout << "[V4L2-DEBUG] Hardware H.264 Encoder successfully initialized!" << std::endl;
    return true;
}

// Generates a moving color gradient natively in YUV420 format
void VideoEncoderThread::generate_test_pattern() {
    int frame_size = res_w * res_h;
    uint8_t* y_plane = v4l2_in_buffer;
    uint8_t* u_plane = v4l2_in_buffer + frame_size;
    uint8_t* v_plane = v4l2_in_buffer + frame_size + (frame_size / 4);

    for (int y = 0; y < res_h; ++y) {
        for (int x = 0; x < res_w; ++x) {
            y_plane[y * res_w + x] = (x + y + frame_counter) % 256;
        }
    }
    for (int y = 0; y < res_h / 2; ++y) {
        for (int x = 0; x < res_w / 2; ++x) {
            u_plane[y * (res_w / 2) + x] = (x + frame_counter) % 256;
            v_plane[y * (res_w / 2) + x] = (y + frame_counter) % 256;
        }
    }
    frame_counter += 5; 
}

// Converts DRM ARGB8888 buffer to YUV420 so the Pi's encoder can digest it
void VideoEncoderThread::convert_rgb_to_yuv420() {
    int frame_size = res_w * res_h;
    uint8_t* y_plane = v4l2_in_buffer;
    uint8_t* u_plane = v4l2_in_buffer + frame_size;
    uint8_t* v_plane = v4l2_in_buffer + frame_size + (frame_size / 4);

    for (int j = 0; j < res_h; ++j) {
        for (int i = 0; i < res_w; ++i) {
            int rgb_idx = (j * res_w + i) * 4;
            // DRM usually exposes BGRA in memory on little-endian ARM
            uint8_t b = drm_mapped_buffer[rgb_idx + 0];
            uint8_t g = drm_mapped_buffer[rgb_idx + 1];
            uint8_t r = drm_mapped_buffer[rgb_idx + 2];

            y_plane[j * res_w + i] = ((66 * r + 129 * g + 25 * b) >> 8) + 16;
            
            if (j % 2 == 0 && i % 2 == 0) {
                int uv_idx = (j / 2) * (res_w / 2) + (i / 2);
                u_plane[uv_idx] = ((-38 * r + -74 * g + 112 * b) >> 8) + 128;
                v_plane[uv_idx] = ((112 * r + -94 * g + -18 * b) >> 8) + 128;
            }
        }
    }
}

void VideoEncoderThread::encode_loop() {
    std::cout << "[HW-STATE] Transmitting Video Frames!" << std::endl;
    int yuv_frame_size = (res_w * res_h * 3) / 2;

    while (running) {
        auto start_time = std::chrono::steady_clock::now();

        if (use_test_pattern) {
            generate_test_pattern();
        } else {
            convert_rgb_to_yuv420();
        }
        
        struct v4l2_buffer buf_out = {};
        buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf_out.memory = V4L2_MEMORY_MMAP;
        buf_out.bytesused = yuv_frame_size; // YUV420 size
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf_out) < 0) continue;

        struct v4l2_buffer buf_cap = {};
        buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf_cap.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_cap) == 0) {
            std::vector<uint8_t> nalu(v4l2_out_buffer, v4l2_out_buffer + buf_cap.bytesused);
            ioctl(v4l2_fd, VIDIOC_QBUF, &buf_cap);
            ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_out); 

            if (!nalu.empty()) {
                std::vector<uint8_t> encrypted = tls_ctx.encrypt(nalu);
                GalFrame video_frame;
                video_frame.channel_id = ch_id;
                video_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_MEDIA;
                
                // Android Auto expects timestamps in Microseconds
                auto pts_now = std::chrono::steady_clock::now().time_since_epoch();
                video_frame.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(pts_now).count();
                video_frame.payload = encrypted;

                usb_transport.write_frame(video_frame);
            }
        }

        // Lock to ~30 FPS
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed.count() < 33) std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed.count()));
    }
}

void VideoEncoderThread::cleanup_hardware() {
    if (v4l2_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        if (v4l2_in_buffer) munmap(v4l2_in_buffer, v4l2_in_len); 
        if (v4l2_out_buffer) munmap(v4l2_out_buffer, v4l2_out_len);
        close(v4l2_fd);
    }
    if (drm_fd >= 0) { 
        if (drm_mapped_buffer) munmap(drm_mapped_buffer, drm_buffer_size); 
        close(drm_fd); 
    }
}