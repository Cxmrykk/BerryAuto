#include "usb_ep0.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include <linux/usb/functionfs.h>
#include <openssl/ssl.h>
#include <sys/syscall.h>
#include <unistd.h>

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
                stop_video_stream();
                flush_usb_tx_queue();
                video_channel_ready = false;
                input_channel_ready = false;
                video_unacked_count = 0;
                is_audio_streaming = false;
                has_audio_focus = false;

                // Scrub TLS data so next rapid connection does not fault on stale symmetric keys
                SSL_clear(ssl);
                BIO_reset(rbio);
                BIO_reset(wbio);
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

                        should_exit = true;
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
