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
    v4l2_fd = open("/dev/video11", O_RDWR);
    if (v4l2_fd < 0) {
        v4l2_fd = open("/dev/video12", O_RDWR);
        if (v4l2_fd < 0) return false;
    }

    struct v4l2_format fmt_out = {};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.width = res_w; 
    fmt_out.fmt.pix_mp.height = res_h;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt_out.fmt.pix_mp.num_planes = 1;
    fmt_out.fmt.pix_mp.field = V4L2_FIELD_ANY;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_out, "Set output format (YUV420 MPLANE)");

    struct v4l2_format fmt_cap = {};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.width = res_w; 
    fmt_cap.fmt.pix_mp.height = res_h;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt_cap.fmt.pix_mp.num_planes = 1;
    fmt_cap.fmt.pix_mp.field = V4L2_FIELD_ANY;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_FMT, &fmt_cap, "Set capture format (H264 MPLANE)");

    struct v4l2_ext_control ctrls[4] = {};
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE; ctrls[0].value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    ctrls[1].id = V4L2_CID_MPEG_VIDEO_BITRATE; ctrls[1].value = 4000000;
    ctrls[2].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD; ctrls[2].value = 30;
    ctrls[3].id = V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER; ctrls[3].value = 1; 
    
    struct v4l2_ext_controls ext_ctrls = {};
    ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG; ext_ctrls.count = 4; ext_ctrls.controls = ctrls;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls, "Set Encoder Controls");

    struct v4l2_requestbuffers req_out = {};
    req_out.count = 1; req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; req_out.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_out, "Request Out Buffers");

    struct v4l2_requestbuffers req_cap = {};
    req_cap.count = 1; req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; req_cap.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_REQBUFS, &req_cap, "Request Cap Buffers");

    struct v4l2_plane out_planes[1] = {};
    struct v4l2_buffer buf_out = {};
    buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; 
    buf_out.memory = V4L2_MEMORY_MMAP; 
    buf_out.index = 0;
    buf_out.length = 1;
    buf_out.m.planes = out_planes;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QUERYBUF, &buf_out, "Query Out Buffer");
    v4l2_in_len = buf_out.m.planes[0].length;
    v4l2_in_buffer = (uint8_t*)mmap(NULL, v4l2_in_len, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_out.m.planes[0].m.mem_offset);

    struct v4l2_plane cap_planes[1] = {};
    struct v4l2_buffer buf_cap = {};
    buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; 
    buf_cap.memory = V4L2_MEMORY_MMAP; 
    buf_cap.index = 0;
    buf_cap.length = 1;
    buf_cap.m.planes = cap_planes;
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QUERYBUF, &buf_cap, "Query Cap Buffer");
    v4l2_out_len = buf_cap.m.planes[0].length;
    v4l2_out_buffer = (uint8_t*)mmap(NULL, v4l2_out_len, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf_cap.m.planes[0].m.mem_offset);

    int type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; 
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_STREAMON, &type_out, "StreamOn Output");
    int type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; 
    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_STREAMON, &type_cap, "StreamOn Capture");

    IOCTL_OR_FAIL(v4l2_fd, VIDIOC_QBUF, &buf_cap, "Queue Initial Capture Buffer");

    std::cout << "[V4L2-DEBUG] Hardware H.264 Encoder successfully initialized!" << std::endl;
    return true;
}

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
}

void VideoEncoderThread::convert_rgb_to_yuv420() {
    int frame_size = res_w * res_h;
    uint8_t* y_plane = v4l2_in_buffer;
    uint8_t* u_plane = v4l2_in_buffer + frame_size;
    uint8_t* v_plane = v4l2_in_buffer + frame_size + (frame_size / 4);

    for (int j = 0; j < res_h; ++j) {
        for (int i = 0; i < res_w; ++i) {
            int rgb_idx = (j * res_w + i) * 4;
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
        
        struct v4l2_plane out_planes[1] = {};
        struct v4l2_buffer buf_out = {};
        buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf_out.memory = V4L2_MEMORY_MMAP;
        buf_out.index = 0;
        buf_out.length = 1;
        buf_out.m.planes = out_planes;
        buf_out.m.planes[0].bytesused = yuv_frame_size;
        
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf_out) < 0) {
            std::cerr << "[V4L2-ERR] QBUF OUTPUT failed: " << strerror(errno) << std::endl;
            continue;
        }

        struct v4l2_plane cap_planes[1] = {};
        struct v4l2_buffer buf_cap = {};
        buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf_cap.memory = V4L2_MEMORY_MMAP;
        buf_cap.length = 1;
        buf_cap.m.planes = cap_planes;
        
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_cap) == 0) {
            size_t bytes_used = buf_cap.m.planes[0].bytesused;
            std::vector<uint8_t> nalu(v4l2_out_buffer, v4l2_out_buffer + bytes_used);
            
            // Re-queue the capture buffer for the next frame
            ioctl(v4l2_fd, VIDIOC_QBUF, &buf_cap);
            // Reclaim the raw buffer we just submitted
            ioctl(v4l2_fd, VIDIOC_DQBUF, &buf_out); 

            if (!nalu.empty()) {
                // 1. Construct Plaintext: [MsgType: 0x0000] [Timestamp: 8 bytes] [NALU]
                std::vector<uint8_t> full_plaintext;
                full_plaintext.reserve(10 + nalu.size());
                
                // 16-bit Media Data type (0x0000)
                full_plaintext.push_back(0x00);
                full_plaintext.push_back(0x00);
                
                auto pts_now = std::chrono::steady_clock::now().time_since_epoch();
                uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(pts_now).count();
                
                // 64-bit Network Byte Order Timestamp
                for (int i = 7; i >= 0; --i) {
                    full_plaintext.push_back((timestamp >> (i * 8)) & 0xFF);
                }
                full_plaintext.insert(full_plaintext.end(), nalu.begin(), nalu.end());

                // 2. Application-Level Fragmentation and Encryption
                size_t max_chunk = 16000;
                size_t offset = 0;
                bool is_fragmented = full_plaintext.size() > max_chunk;
                uint32_t total_size = full_plaintext.size();

                while (offset < full_plaintext.size()) {
                    size_t chunk_size = std::min(max_chunk, full_plaintext.size() - offset);
                    std::vector<uint8_t> chunk(full_plaintext.begin() + offset, full_plaintext.begin() + offset + chunk_size);
                    
                    std::vector<uint8_t> encrypted_chunk = tls_ctx.encrypt(chunk);

                    uint8_t flags = FLAG_ENCRYPTED;
                    bool is_first = (offset == 0);
                    bool is_last = (offset + chunk_size >= full_plaintext.size());

                    if (!is_fragmented) {
                        flags |= FLAG_FIRST | FLAG_LAST;
                    } else {
                        if (is_first) flags |= FLAG_FIRST;
                        if (is_last) flags |= FLAG_LAST;
                    }

                    // Serialize the custom fragmented GAL Frame
                    size_t header_size = (is_first && is_fragmented) ? 8 : 4;
                    std::vector<uint8_t> buffer(header_size + encrypted_chunk.size());

                    buffer[0] = ch_id;
                    buffer[1] = flags;
                    buffer[2] = (encrypted_chunk.size() >> 8) & 0xFF;
                    buffer[3] = encrypted_chunk.size() & 0xFF;

                    if (is_first && is_fragmented) {
                        buffer[4] = (total_size >> 24) & 0xFF;
                        buffer[5] = (total_size >> 16) & 0xFF;
                        buffer[6] = (total_size >> 8) & 0xFF;
                        buffer[7] = total_size & 0xFF;
                    }

                    std::memcpy(buffer.data() + header_size, encrypted_chunk.data(), encrypted_chunk.size());
                    usb_transport.write_frame_raw(buffer);

                    offset += chunk_size;
                }

                if (frame_counter % 30 == 0) {
                    std::cout << "[HW-STATE] Sent H.264 Video Frame! (" << nalu.size() << " bytes, " 
                              << (is_fragmented ? "Fragmented" : "Single") << ")" << std::endl;
                }
                
                frame_counter += 5;
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed.count() < 33) std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed.count()));
    }
}

void VideoEncoderThread::cleanup_hardware() {
    if (v4l2_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        if (v4l2_in_buffer) munmap(v4l2_in_buffer, v4l2_in_len); 
        if (v4l2_out_buffer) munmap(v4l2_out_buffer, v4l2_out_len);
        close(v4l2_fd);
    }
    if (drm_fd >= 0) { 
        if (drm_mapped_buffer) munmap(drm_mapped_buffer, drm_buffer_size); 
        close(drm_fd); 
    }
}