#pragma once
#include <mutex>
#include <atomic>
#include <iostream>
#include <openssl/ssl.h>

class VideoEncoder;

extern int ep_in;
extern int ep_out;
extern SSL_CTX *ssl_ctx;
extern SSL *ssl;
extern BIO *rbio;
extern BIO *wbio;
extern bool is_tls_connected;
extern VideoEncoder* video_streamer;
extern bool video_channel_ready;
extern bool input_channel_ready;

// Video and Touch Globals
extern int global_video_width;
extern int global_video_height;
extern int global_video_margin_w;
extern int global_video_margin_h;
extern int global_touch_width;
extern int global_touch_height;

extern std::recursive_mutex aap_mutex;
extern std::atomic<int> video_unacked_count;
extern std::atomic<bool> is_video_streaming;
extern int max_video_unacked;

#define LOG_I(msg) do { std::cout << "[INFO] " << msg << std::endl; } while(0)
#define LOG_E(msg) do { std::cerr << "[ERROR] " << msg << std::endl; } while(0)