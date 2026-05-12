#pragma once
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <openssl/ssl.h>
#include <queue>
#include <string>

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

extern std::atomic<bool> should_exit;

void stop_video_stream();

extern int video_channel_id;
extern int input_channel_id;
extern int audio_channel_id;
extern int mic_channel_id;

extern std::atomic<bool> is_audio_streaming;
extern std::atomic<bool> has_audio_focus;

extern int os_desktop_width;
extern int os_desktop_height;

extern int global_video_config_index;
extern int global_video_codec_type;
extern int global_video_fps;
extern int global_video_width;
extern int global_video_height;
extern int global_video_margin_w;
extern int global_video_margin_h;
extern int global_touch_width;
extern int global_touch_height;

// User Configuration Overrides
extern std::string user_config_video_encoder;
extern std::string user_config_video_profile;
extern std::string user_config_video_preset;
extern std::string user_config_video_tune;
extern int user_config_video_bitrate;
extern int user_config_force_width;
extern int user_config_force_height;
extern int user_config_force_fps;
extern bool user_config_disable_hw_encoding;

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

extern std::recursive_mutex aap_mutex;
extern std::atomic<int> video_unacked_count;
extern std::atomic<bool> is_video_streaming;
extern int max_video_unacked;

extern std::atomic<bool> encoder_teardown_in_progress;
extern std::atomic<int> video_session_id;

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
