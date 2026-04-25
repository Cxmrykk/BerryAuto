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

#define IOCTL_OR_FAIL(fd, req, arg, msg) \
    if (ioctl(fd, req, arg) < 0) { std::cerr << msg << "\n"; return false; }

VideoEncoderThread::VideoEncoderThread(FunctionFSTransport& transport, OpenGALTlsContext& tls, int channel_id)
    : usb_transport(transport), tls_ctx(tls), ch_id(channel_id), running(false) {}

VideoEncoderThread::~VideoEncoderThread() { stop(); cleanup_hardware(); }

void VideoEncoderThread::start(int width, int height) {
    res_w = width; res_h = height;
    if (init_drm_capture() && init_v4l2_encoder()) {
        running = true;
        worker = std::thread(&VideoEncoderThread::encode_loop, this);
    } else {
        std::cerr << "Failed to init Hardware Pi pipeline.\n";
    }
}

void VideoEncoderThread::stop() {
    running = false;
    if (worker.joinable()) worker.join();
}

bool VideoEncoderThread::init_drm_capture() {
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) return false;
    drmModeRes* res = drmModeGetResources(drm_fd);
    if (!res || res->count_crtcs == 0) return false;
    drmModeCrtc* crtc = drmModeGetCrtc(drm_fd, res->crtcs[0]);
    if (!crtc || !crtc->buffer_id) return false;
    fb_id = crtc->buffer_id;
    drmModeFB* fb = drmModeGetFB(drm_fd, fb_id);
    
    // Fix: Calculate size properly via pitch * height
    drm_buffer_size = fb->pitch * fb->height;

    struct drm_mode_map_dumb map_req = {}; 
    map_req.handle = fb->handle;
    ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req);
    drm_mapped_buffer = (uint8_t*)mmap(0, drm_buffer_size, PROT_READ, MAP_SHARED, drm_fd, map_req.offset);
    
    drmModeFreeFB(fb); drmModeFreeCrtc(crtc); drmModeFreeResources(res);
    return (drm_mapped_buffer != MAP_FAILED);
}

bool VideoEncoderThread::init_v4l2_encoder() {
    v4l2_fd = open("/dev/video11", O_RDWR | O_NONBLOCK);
    if (v4l2_fd < 0) return false;

    struct v4l2_format fmt_out = {};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt_out.fmt.pix.width = res_w; fmt_out.fmt.pix.height = res_h;
    fmt_out.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_out, "V4L2 In format err");

    struct v4l2_format fmt_cap = {};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt_cap.fmt.pix.width = res_w; fmt_cap.fmt.pix.height = res_h;
    fmt_cap.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_cap, "V4L2 Out format err");

    struct v4l2_ext_control ctrls[3] = {};
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE; ctrls[0].value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    ctrls[1].id = V4L2_CID_MPEG_VIDEO_BITRATE; ctrls[1].value = 4000000;
    ctrls[2].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD; ctrls[2].value = 30;
    
    struct v4l2_ext_controls ext_ctrls = {};
    ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG; ext_ctrls.count = 3; ext_ctrls.controls = ctrls;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls, "V4L2 Ctrls err");

    // Fix: Explicitly initialize structs to clear GCC warnings
    struct v4l2_requestbuffers req_out = {};
    req_out.count = 1;
    req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req_out.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_out, "V4L2 REQ OUT");

    struct v4l2_requestbuffers req_cap = {};
    req_cap.count = 1;
    req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req_cap.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_cap, "V4L2 REQ CAP");

    struct v4l2_buffer buf_out = {};
    buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf_out.memory = V4L2_MEMORY_MMAP;
    ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf_out);
    v4l2_in_len = buf_out.length;
    v4l2_in_buffer = (uint8_t*)mmap(NULL, buf_out.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_out.m.offset);

    struct v4l2_buffer buf_cap = {};
    buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf_cap.memory = V4L2_MEMORY_MMAP;
    ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf_cap);
    v4l2_out_len = buf_cap.length;
    v4l2_out_buffer = (uint8_t*)mmap(NULL, buf_cap.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_cap.m.offset);

    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT; ioctl(v4l2_fd, VIDIOC_STREAMON, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE; ioctl(v4l2_fd, VIDIOC_STREAMON, &type);
    ioctl(v4l2_fd, VIDIOC_QBUF, &buf_cap);

    return true;
}

void VideoEncoderThread::encode_loop() {
    std::cout << "DRM -> V4L2 Hardware Streaming Started." << std::endl;
    while (running) {
        auto start_time = std::chrono::steady_clock::now();

        std::memcpy(v4l2_in_buffer, drm_mapped_buffer, std::min(drm_buffer_size, v4l2_in_len));
        
        struct v4l2_buffer buf_out = {};
        buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf_out.memory = V4L2_MEMORY_MMAP;
        buf_out.bytesused = v4l2_in_len;
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf_out) < 0) continue;

        struct v4l2_buffer buf_cap = {};
        buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf_cap.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_cap) == 0) {
            std::vector<uint8_t> nalu(v4l2_out_buffer, v4l2_out_buffer + buf_cap.bytesused);
            ioctl(v4l2_fd, VIDIOC_QBUF, &buf_cap);
            ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_out); 

            std::vector<uint8_t> encrypted = tls_ctx.encrypt(nalu);
            GalFrame video_frame;
            video_frame.channel_id = ch_id;
            video_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_MEDIA;
            auto pts_now = std::chrono::steady_clock::now().time_since_epoch();
            video_frame.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(pts_now).count();
            video_frame.payload = encrypted;

            usb_transport.write_frame(video_frame);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed.count() < 33) std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed.count()));
    }
}

void VideoEncoderThread::cleanup_hardware() {
    if (v4l2_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        munmap(v4l2_in_buffer, v4l2_in_len); munmap(v4l2_out_buffer, v4l2_out_len);
        close(v4l2_fd);
    }
    if (drm_fd >= 0) { munmap(drm_mapped_buffer, drm_buffer_size); close(drm_fd); }
}