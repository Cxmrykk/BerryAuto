#include <algorithm>
#include <atomic>
#include <chrono>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <linux/usb/functionfs.h>
#include <mutex>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "aap_sender.hpp"
#include "certs.hpp"
#include "ffs_usb.hpp"
#include "globals.hpp"
#include "input_handler.hpp"
#include "message_handler.hpp"
#include "video_encoder.hpp"

// Global Instantiations
int ep_in, ep_out;
SSL_CTX* ssl_ctx;
SSL* ssl;
BIO *rbio, *wbio;
bool is_tls_connected = false;
bool ssl_bypassed = false;
VideoEncoder* video_streamer = nullptr;
bool video_channel_ready = false;
bool input_channel_ready = false;

// Dynamic Channel Assignments
int video_channel_id = 2;
int input_channel_id = 3;

std::queue<int> pending_channel_opens;
std::map<int, ChannelType> channel_types;

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

void force_zero_ack(int ep0, bool is_in)
{
    if (is_in)
    {
        syscall(SYS_write, ep0, NULL, 0);
    }
    else
    {
        syscall(SYS_read, ep0, NULL, 0);
    }
}

void ep0_thread(int ep0)
{
    struct usb_functionfs_event event;

    while (true)
    {
        int r = read(ep0, &event, sizeof(event));
        if (r < 0)
        {
            usleep(10000);
            continue;
        }
        switch (event.type)
        {
            case FUNCTIONFS_BIND:
                LOG_I("[USB] Gadget Bound to Host");
                break;
            case FUNCTIONFS_ENABLE:
                LOG_I("[USB] Configured & Enabled!");
                break;
            case FUNCTIONFS_DISABLE:
            case FUNCTIONFS_UNBIND:
            {
                LOG_I("[USB] Disabled/Unbound by Host (Port Reset/Suspend)");
                std::lock_guard<std::recursive_mutex> lock(aap_mutex);
                is_tls_connected = false;
                ssl_bypassed = false;
                is_video_streaming = false;
                video_channel_ready = false;
                input_channel_ready = false;
                video_unacked_count = 0;
                break;
            }
            case FUNCTIONFS_SETUP:
            {
                auto& setup = event.u.setup;

                if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR)
                {
                    if (setup.bRequest == 51)
                    {
                        uint16_t version = 1;
                        write(ep0, &version, 2);
                        LOG_I("[AOA] Answered GET_PROTOCOL (51) with Version 1.0");
                    }
                    else if (setup.bRequest == 52)
                    {
                        char str_buf[256];
                        memset(str_buf, 0, sizeof(str_buf));

                        if (setup.wLength > 0)
                        {
                            int to_read = std::min((int)setup.wLength, 255);
                            read(ep0, str_buf, to_read);
                        }
                        else
                        {
                            force_zero_ack(ep0, false);
                        }
                        LOG_I("[AOA] Received SEND_STRING (index " << setup.wIndex << "): " << str_buf);
                    }
                    else if (setup.bRequest == 53)
                    {
                        force_zero_ack(ep0, false);
                        LOG_I("[AOA] Received START (53). Acknowledged. Waiting 500ms to flush, then morphing...");
                        usleep(500000);
                        exit(42);
                    }
                    else
                    {
                        force_zero_ack(ep0, (setup.bRequestType & USB_DIR_IN));
                    }
                }
                else
                {
                    force_zero_ack(ep0, (setup.bRequestType & USB_DIR_IN));
                }
                break;
            }
        }
    }
}

int main()
{
    LOG_I("Starting OpenGAL Emitter...");

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

    std::vector<uint8_t> usb_rx_buffer;
    uint8_t tmp_buf[16384];

    while (true)
    {
        int r = read(ep_out, tmp_buf, sizeof(tmp_buf));

        if (r < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                usleep(1000);
                continue;
            }
            if (errno == ESHUTDOWN || errno == ENODEV)
            {
                usleep(10000);
                continue;
            }
            LOG_E("[BULK-RX] Read error: " << strerror(errno));
            usleep(100000);
            continue;
        }
        if (r == 0)
        {
            usleep(1000);
            continue;
        }

        if (!is_video_streaming.load())
        {
            LOG_I("[BULK-RX] Read " << r << " raw bytes from USB.");
        }

        usb_rx_buffer.insert(usb_rx_buffer.end(), tmp_buf, tmp_buf + r);

        while (usb_rx_buffer.size() >= 4)
        {
            uint16_t len = (usb_rx_buffer[2] << 8) | usb_rx_buffer[3];

            if (usb_rx_buffer.size() >= (size_t)(4 + len))
            {
                uint8_t channel = usb_rx_buffer[0];
                uint8_t flags = usb_rx_buffer[1];
                std::vector<uint8_t> payload(usb_rx_buffer.begin() + 4, usb_rx_buffer.begin() + 4 + len);

                usb_rx_buffer.erase(usb_rx_buffer.begin(), usb_rx_buffer.begin() + 4 + len);

                if (channel != 2)
                {
                    std::cout << "[DEBUG-RX] Raw Packet - Channel: " << (int)channel << " Flags: 0x" << std::hex
                              << (int)flags << std::dec << " Len: " << len << std::endl;
                }

                // Use robust bitmasking to detect encrypted messages
                if ((flags & 0x08) != 0)
                {
                    {
                        std::lock_guard<std::recursive_mutex> lock(aap_mutex);

                        size_t offset = 0;
                        if ((flags & 0x01) != 0 && (flags & 0x02) == 0)
                        { // 0x09
                            if (payload.size() >= 4)
                            {
                                offset = 4;
                            }
                        }

                        if (payload.size() > offset)
                        {
                            BIO_write(rbio, payload.data() + offset, payload.size() - offset);

                            uint8_t dec_buf[16384];
                            while (true)
                            {
                                int dec_bytes = SSL_read(ssl, dec_buf, sizeof(dec_buf));
                                if (dec_bytes > 0)
                                {
                                    if (dec_bytes >= 2)
                                    {
                                        uint16_t type = (dec_buf[0] << 8) | dec_buf[1];
                                        handle_decrypted_payload(channel, type, dec_buf + 2, dec_bytes - 2);
                                    }
                                    else
                                    {
                                        LOG_I("[DEBUG] SSL_read output too small for AAP header: " << dec_bytes
                                                                                                   << " bytes");
                                    }
                                }
                                else
                                {
                                    int err = SSL_get_error(ssl, dec_bytes);
                                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                                    {
                                        LOG_E(">>> SSL_read Decryption Error: " << err << " <<<");
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    ssl_write_and_flush_unlocked({}, 0, 0x0B, 0);
                }
                else
                {
                    if (payload.size() >= 2)
                    {
                        uint16_t type = (payload[0] << 8) | payload[1];
                        handle_unencrypted_payload(channel, type, payload.data() + 2, payload.size() - 2);
                    }
                    else
                    {
                        LOG_I("[DEBUG] Unencrypted payload too small for AAP header: " << payload.size() << " bytes");
                    }
                }
            }
            else
            {
                break;
            }
        }
    }

    cleanup_input();
    return 0;
}