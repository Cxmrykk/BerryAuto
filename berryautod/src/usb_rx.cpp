#include "usb_rx.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include "input_handler.hpp"
#include "message_handler.hpp"
#include <iostream>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <unistd.h>
#include <vector>

int usb_rx_loop()
{
    std::vector<uint8_t> usb_rx_buffer;
    uint8_t tmp_buf[16384];

    while (true)
    {
        if (should_exit.load())
            break;

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

                if ((flags & 0x08) != 0)
                {
                    {
                        std::lock_guard<std::recursive_mutex> lock(aap_mutex);

                        size_t offset = 0;
                        if ((flags & 0x01) != 0 && (flags & 0x02) == 0)
                        {
                            if (payload.size() >= 4)
                                offset = 4;
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
                                }
                                else
                                {
                                    int err = SSL_get_error(ssl, dec_bytes);
                                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                                    {
                                        LOG_E(">>> SSL_read Decryption Error: " << err << " <<<");
                                        SSL_clear(ssl);
                                        is_tls_connected = false;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    flush_ssl_buffers();
                }
                else
                {
                    if (payload.size() >= 2)
                    {
                        uint16_t type = (payload[0] << 8) | payload[1];
                        handle_unencrypted_payload(channel, type, payload.data() + 2, payload.size() - 2);
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

    if (should_exit.load())
        return 42;
    return 0;
}
