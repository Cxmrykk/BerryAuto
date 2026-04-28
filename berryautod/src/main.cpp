#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <sys/stat.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <thread>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <linux/usb/functionfs.h>
#include <linux/usb/ch9.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "globals.hpp"
#include "certs.hpp"
#include "ffs_usb.hpp"
#include "video_encoder.hpp"
#include "aap_sender.hpp"
#include "message_handler.hpp"
#include "input_handler.hpp"

// Global Instantiations
int ep_in, ep_out;
SSL_CTX *ssl_ctx;
SSL *ssl;
BIO *rbio, *wbio;
bool is_tls_connected = false;
VideoEncoder* video_streamer = nullptr;
bool video_channel_ready = false;
bool input_channel_ready = false;

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

bool load_hardcoded_certs(SSL_CTX *ctx) {
    BIO* cert_bio = BIO_new_mem_buf(AAP_CERT, -1);
    X509* cert = PEM_read_bio_X509(cert_bio, NULL, 0, NULL);
    if (!cert || SSL_CTX_use_certificate(ctx, cert) <= 0) return false;
    
    BIO* key_bio = BIO_new_mem_buf(AAP_KEY, -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, NULL, 0, NULL);
    if (!pkey || SSL_CTX_use_PrivateKey(ctx, pkey) <= 0) return false;
    
    BIO_free(cert_bio);
    BIO_free(key_bio);
    return true;
}

// Dedicated thread to read and respond to EP0 (Control Endpoint) events
// This intercepts the Head Unit's AOA (Android Open Accessory) negotiation requests
void ep0_thread(int ep0) {
    struct usb_functionfs_event event;
    while (true) {
        int r = read(ep0, &event, sizeof(event));
        if (r < 0) {
            LOG_E("[EP0] Read failed: " << strerror(errno));
            usleep(1000000); // Sleep 1s and retry
            continue; 
        }

        switch (event.type) {
            case FUNCTIONFS_BIND: LOG_I("[EP0] Event: BIND"); break;
            case FUNCTIONFS_UNBIND: LOG_I("[EP0] Event: UNBIND"); break;
            case FUNCTIONFS_ENABLE: LOG_I("[EP0] Event: ENABLE (Host successfully configured the gadget)"); break;
            case FUNCTIONFS_DISABLE: LOG_I("[EP0] Event: DISABLE (Host unconfigured the gadget)"); break;
            case FUNCTIONFS_SUSPEND: LOG_I("[EP0] Event: SUSPEND"); break;
            case FUNCTIONFS_RESUME: LOG_I("[EP0] Event: RESUME"); break;
            case FUNCTIONFS_SETUP: {
                auto setup = event.u.setup;
                
                // Intercept Android Open Accessory (AOA) Negotiation
                if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
                    if (setup.bRequest == 51) { // ACC_REQ_GET_PROTOCOL
                        LOG_I("[AOA] Car requested protocol version. Sending AOA 2.0");
                        uint16_t version = htole16(2);
                        write(ep0, &version, 2);
                    } else if (setup.bRequest == 52) { // ACC_REQ_SEND_STRING
                        uint16_t len = le16toh(setup.wLength);
                        if (len > 0) {
                            std::vector<uint8_t> buf(len);
                            read(ep0, buf.data(), len);
                            std::string str(buf.begin(), buf.end());
                            LOG_I("[AOA] Car sent string (Index " << le16toh(setup.wIndex) << "): " << str);
                        } else {
                            read(ep0, nullptr, 0);
                        }
                    } else if (setup.bRequest == 53) { // ACC_REQ_START
                        LOG_I("[AOA] Car requested accessory start!");
                        read(ep0, nullptr, 0); // ACK the request
                        
                        LOG_I(">>> Bouncing USB Gadget to Accessory Mode (0x2D00) <<<");
                        // Execute the bounce. FunctionFS endpoints survive this rebind!
                        system("echo \"\" > /sys/kernel/config/usb_gadget/opengal/UDC; "
                               "sleep 0.5; "
                               "echo 0x2D00 > /sys/kernel/config/usb_gadget/opengal/idProduct; "
                               "sleep 0.5; "
                               "ls /sys/class/udc | head -n 1 > /sys/kernel/config/usb_gadget/opengal/UDC");
                    } else {
                        // Unknown vendor request, stall
                        if (setup.bRequestType & USB_DIR_IN) read(ep0, nullptr, 0); 
                        else read(ep0, nullptr, 0);
                    }
                } else {
                    // Non-Vendor Setup Request. Stall to delegate standard requests to the kernel.
                    if (setup.bRequestType & USB_DIR_IN) read(ep0, nullptr, 0); 
                    else read(ep0, nullptr, 0); 
                }
                break;
            }
            default:
                LOG_I("[EP0] Unknown Event (" << event.type << ")");
        }
    }
}

int main() {
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

    SSL_CTX_set_session_id_context(ssl_ctx, (const unsigned char*)"AA", 2);
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_tlsext_ticket_keys(ssl_ctx, aa_ticket_keys, sizeof(aa_ticket_keys));

    if (!load_hardcoded_certs(ssl_ctx)) {
        LOG_E("Failed to load Google Certificates. Exiting.");
        return 1;
    }
    
    ssl = SSL_new(ssl_ctx);
    rbio = BIO_new(BIO_s_mem());
    wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl, rbio, wbio);
    SSL_set_accept_state(ssl); 

    int ep0;
    if (!init_ffs(ep0, ep_in, ep_out)) {
        LOG_E("Failed to initialize FFS endpoints!");
        return 1;
    }
    
    // Spawn the EP0 event thread in the background
    std::thread ep0_t(ep0_thread, ep0);
    ep0_t.detach();
    
    LOG_I("USB Endpoints bound successfully. Waiting for Car/Tablet to connect...");

    std::vector<uint8_t> usb_rx_buffer;
    uint8_t tmp_buf[16384];

    while (true) {
        int r = read(ep_out, tmp_buf, sizeof(tmp_buf));
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                usleep(1000); // 1ms sleep for non-blocking interruptions
                continue;
            }
            // ESHUTDOWN (108) happens when the port suspends or during the AOA bounce.
            // Just sleep and loop until the connection comes back up.
            LOG_E("[USB IN] Bulk Read failed! Errno: " << errno << " (" << strerror(errno) << ")");
            usleep(500000); 
            continue;
        }
        if (r == 0) {
            usleep(1000);
            continue;
        }
        
        usb_rx_buffer.insert(usb_rx_buffer.end(), tmp_buf, tmp_buf + r);

        while (usb_rx_buffer.size() >= 4) {
            uint16_t len = (usb_rx_buffer[2] << 8) | usb_rx_buffer[3];
            
            if (usb_rx_buffer.size() >= (size_t)(4 + len)) {
                uint8_t channel = usb_rx_buffer[0];
                uint8_t flags   = usb_rx_buffer[1];
                std::vector<uint8_t> payload(usb_rx_buffer.begin() + 4, usb_rx_buffer.begin() + 4 + len);
                
                usb_rx_buffer.erase(usb_rx_buffer.begin(), usb_rx_buffer.begin() + 4 + len);

                if (flags == 0x0B || flags == 0x08 || flags == 0x0A || flags == 0x09) {
                    {
                        std::lock_guard<std::recursive_mutex> lock(aap_mutex);
                        BIO_write(rbio, payload.data(), payload.size());
                        
                        uint8_t dec_buf[16384];
                        while (true) {
                            int dec_bytes = SSL_read(ssl, dec_buf, sizeof(dec_buf));
                            if (dec_bytes > 0) {
                                if (dec_bytes >= 2) {
                                    uint16_t type = (dec_buf[0] << 8) | dec_buf[1];
                                    handle_decrypted_payload(channel, type, dec_buf + 2, dec_bytes - 2);
                                }
                            } else {
                                break; 
                            }
                        }
                    }
                    ssl_write_and_flush_unlocked({}, 0, 0x0B, 0); 
                } else {
                    if (payload.size() >= 2) {
                        uint16_t type = (payload[0] << 8) | payload[1];
                        handle_unencrypted_payload(channel, type, payload.data() + 2, payload.size() - 2);
                    }
                }
            } else {
                break;
            }
        }
    }
    
    cleanup_input();
    LOG_I("[SHUTDOWN] Exiting...");
    return 0;
}