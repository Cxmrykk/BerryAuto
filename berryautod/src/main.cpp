#include <atomic>
#include <iostream>
#include <openssl/ssl.h>
#include <thread>
#include <unistd.h>

#include "audio_alsa.hpp"
#include "certs.hpp"
#include "config_parser.hpp"
#include "ffs_usb.hpp"
#include "globals.hpp"
#include "usb_ep0.hpp"
#include "usb_rx.hpp"
#include "video_encoder.hpp"
#include "video_sender.hpp"

// Config definitions
std::string user_config_video_encoder = "";
std::string user_config_video_profile = "";
std::string user_config_video_preset = "";
std::string user_config_video_tune = "";
int user_config_video_bitrate = 0;
int user_config_force_width = 0;
int user_config_force_height = 0;
int user_config_force_fps = 0;
bool user_config_disable_hw_encoding = false;

int ep_in, ep_out;
SSL_CTX* ssl_ctx;
SSL* ssl;
BIO *rbio, *wbio;
bool is_tls_connected = false;
bool ssl_bypassed = false;
VideoEncoder* video_streamer = nullptr;
bool video_channel_ready = false;
bool input_channel_ready = false;

std::atomic<bool> should_exit{false};

int video_channel_id = 2;
int input_channel_id = 3;
int audio_channel_id = -1;
int mic_channel_id = -1;

std::atomic<bool> is_audio_streaming{false};
std::atomic<bool> has_audio_focus{false};

std::queue<int> pending_channel_opens;
std::map<int, ChannelType> channel_types;

int os_desktop_width = 800;
int os_desktop_height = 480;

int global_video_config_index = 0;
int global_video_codec_type = 3;
int global_video_fps = 60;
int global_video_width = 800;
int global_video_height = 480;
int global_video_margin_w = 0;
int global_video_margin_h = 0;
int global_touch_width = 800;
int global_touch_height = 480;

std::recursive_mutex aap_mutex;
std::atomic<int> video_unacked_count{0};
std::atomic<bool> is_video_streaming{false};
int max_video_unacked = 16;

// Thread-safe state definitions
std::atomic<bool> encoder_teardown_in_progress{false};
std::atomic<int> video_session_id{0};

void stop_video_stream()
{
    is_video_streaming = false;
    reset_video_sender_state();

    if (video_streamer != nullptr)
    {
        VideoEncoder* enc = video_streamer;
        video_streamer = nullptr;
        encoder_teardown_in_progress = true;

        // Detached thread prevents deadlocking the USB Ping/RX Loop
        std::thread(
            [enc]()
            {
                enc->stop();
                delete enc;
                encoder_teardown_in_progress = false;
            })
            .detach();
    }
}

bool load_hardcoded_certs(SSL_CTX* ctx)
{
    BIO* cert_bio = BIO_new_mem_buf(AAP_CERT, -1);
    X509* cert = PEM_read_bio_X509(cert_bio, NULL, 0, NULL);
    if (!cert || SSL_CTX_use_certificate(ctx, cert) <= 0)
        return false;

    BIO* key_bio = BIO_new_mem_buf(AAP_KEY, -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, NULL, 0, NULL);
    if (!pkey || SSL_CTX_use_PrivateKey(ctx, pkey) <= 0)
        return false;

    BIO_free(cert_bio);
    BIO_free(key_bio);
    return true;
}

int dummy_verify_cb(int preverify_ok, X509_STORE_CTX* ctx)
{
    (void)preverify_ok;
    (void)ctx;
    return 1;
}

int main()
{
    LOG_I("Starting OpenGAL Emitter...");

    load_config();

    os_desktop_width = 800;
    os_desktop_height = 480;

    SSL_library_init();
    SSL_load_error_strings();
    ssl_ctx = SSL_CTX_new(TLS_server_method());

    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
#ifdef SSL_OP_NO_TLSv1_3
    options |= SSL_OP_NO_TLSv1_3;
#endif
    SSL_CTX_set_options(ssl_ctx, options);
    SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, dummy_verify_cb);
    SSL_CTX_set_session_id_context(ssl_ctx, (const unsigned char*)"AA", 2);
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_tlsext_ticket_keys(ssl_ctx, aa_ticket_keys, sizeof(aa_ticket_keys));

    if (!load_hardcoded_certs(ssl_ctx))
    {
        LOG_E("Failed to load Google Certificates.");
        return 1;
    }

    if (!init_alsa())
    {
        LOG_E("Failed to initialize ALSA subsystem! Are virtual loopback devices present?");
        // We can continue running without audio, but log heavily.
    }

    ssl = SSL_new(ssl_ctx);
    rbio = BIO_new(BIO_s_mem());
    wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl, rbio, wbio);
    SSL_set_accept_state(ssl);

    int ep0;
    if (!init_ffs(ep0, ep_in, ep_out))
    {
        LOG_E("Failed to initialize FFS endpoints!");
        return 1;
    }

    std::thread ep0_t(ep0_thread, ep0);
    ep0_t.detach();

    int exit_code = usb_rx_loop();
    stop_alsa();
    return exit_code;
}
