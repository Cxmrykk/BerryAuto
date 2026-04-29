#pragma once
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <openssl/ssl.h>
#include <queue>

class VideoEncoder;

extern int ep_in;
extern int ep_out;
extern SSL_CTX* ssl_ctx;
extern SSL* ssl;
extern BIO* rbio;
extern BIO* wbio;
extern bool is_tls_connected;
extern bool ssl_bypassed;
extern VideoEncoder* video_streamer;
extern bool video_channel_ready;
extern bool input_channel_ready;

// Dynamic AAP Channels
extern int video_channel_id;
extern int input_channel_id;

// Dynamic Channel State Management
enum class ChannelType
{
    UNKNOWN,
    VIDEO,
    AUDIO,
    MIC,
    INPUT,
    SENSOR,
    BLUETOOTH,
    NAVIGATION,
    STATUS
};
extern std::map<int, ChannelType> channel_types;
extern std::queue<int> pending_channel_opens;

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

#define LOG_I(msg)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "[INFO] " << msg << std::endl;                                                                    \
    } while (0)
#define LOG_E(msg)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cerr << "[ERROR] " << msg << std::endl;                                                                   \
    } while (0)