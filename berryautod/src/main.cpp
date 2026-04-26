#include "crypto_tls.h"
#include "input_injector.h"
#include "opengal.pb.h"
#include "transport_ffs.h"
#include "video_encoder.h"
#include <iomanip>
#include <iostream>
#include <memory>

using namespace opengal;

int main()
{
    std::cout << "[MAIN] Starting OpenGAL Linux Emitter..." << std::endl;

    FunctionFSTransport usb_transport("/dev/ffs-opengal");
    if (!usb_transport.init())
        return 1;

    OpenGALTlsContext tls_ctx;
    
    // We will allocate the video thread dynamically once we know the assigned channel ID
    std::unique_ptr<VideoEncoderThread> video_thread;
    InputInjector touch;

    bool auth_complete = false;

    std::cout << "[MAIN] Listening for frames..." << std::endl;

    while (true)
    {
        auto frames = usb_transport.read_frames();
        for (const auto& frame : frames)
        {
            if (frame.channel_id == 0 && !auth_complete)
            {
                uint16_t msg_type = (frame.payload[0] << 8) | frame.payload[1];

                if (msg_type == 1)
                { // VersionRequest -> VersionResponse
                    std::cout << "[MAIN] <- [Ch 0] Received VersionRequest." << std::endl;
                    GalFrame resp;
                    resp.channel_id = 0;
                    resp.flags = FLAG_FIRST | FLAG_LAST;
                    resp.payload = {0x00, 0x02, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00};
                    std::cout << "[MAIN] -> [Ch 0] Sending VersionResponse (Major: 1, Minor: 6)." << std::endl;
                    usb_transport.write_frame(resp);
                }
                else if (msg_type == 3)
                { // SslHandshake
                    std::cout << "[MAIN] <- [Ch 0] Received SslHandshake Record (" << frame.payload.size() - 2
                              << " bytes)." << std::endl;
                    std::vector<uint8_t> tls_input(frame.payload.begin() + 2, frame.payload.end());
                    std::vector<uint8_t> tls_output;
                    bool is_finished = tls_ctx.do_handshake(tls_input, tls_output);

                    if (!tls_output.empty())
                    {
                        std::cout << "[MAIN] -> [Ch 0] Sending SslHandshake Reply (" << tls_output.size() << " bytes)."
                                  << std::endl;
                        GalFrame resp;
                        resp.channel_id = 0;
                        resp.flags = FLAG_FIRST | FLAG_LAST;
                        resp.payload.push_back(0x00);
                        resp.payload.push_back(0x03);
                        resp.payload.insert(resp.payload.end(), tls_output.begin(), tls_output.end());
                        usb_transport.write_frame(resp);
                    }

                    if (is_finished)
                    {
                        std::cout << "[MAIN] *** TLS Handshake Complete! Transitioning to Encrypted Mode. ***"
                                  << std::endl;

                        // 1. Send AuthComplete (Type 4)
                        AuthComplete auth;
                        auth.set_status(STATUS_SUCCESS);
                        std::string auth_str = auth.SerializeAsString();
                        std::vector<uint8_t> pt(auth_str.begin(), auth_str.end());
                        pt.insert(pt.begin(), {0x00, 0x04});

                        GalFrame auth_frame;
                        auth_frame.channel_id = 0;
                        auth_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        auth_frame.payload = tls_ctx.encrypt(pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending AuthComplete (Encrypted)." << std::endl;
                        usb_transport.write_frame(auth_frame);
                        
                        auth_complete = true;

                        // 2. Send Service Discovery Request (Type 6)
                        // By sending a mostly empty ServiceDiscovery payload, we request the Car 
                        // to report its supported services and capabilities.
                        ServiceDiscovery sdp_req;
                        std::string sdp_req_str = sdp_req.SerializeAsString();
                        std::vector<uint8_t> sdp_req_pt(sdp_req_str.begin(), sdp_req_str.end());
                        sdp_req_pt.insert(sdp_req_pt.begin(), {0x00, 0x06});

                        GalFrame sdp_frame;
                        sdp_frame.channel_id = 0;
                        sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        sdp_frame.payload = tls_ctx.encrypt(sdp_req_pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending ServiceDiscoveryRequest." << std::endl;
                        usb_transport.write_frame(sdp_frame);
                    }
                }
            }
            else if (auth_complete && (frame.flags & FLAG_ENCRYPTED))
            {
                std::vector<uint8_t> plaintext = tls_ctx.decrypt(frame.payload);
                if (plaintext.size() < 2)
                    continue;

                uint16_t msg_type = (plaintext[0] << 8) | plaintext[1];

                if (frame.channel_id == 0)
                {
                    if (msg_type == 6)
                    { // ServiceDiscoveryResponse (From Head Unit)
                        std::cout << "[MAIN] <- [Ch 0] Received ServiceDiscoveryResponse from Head Unit." << std::endl;
                        ServiceDiscovery sdp_resp;
                        sdp_resp.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);

                        std::cout << "[MAIN] Connected to Head Unit: " << sdp_resp.head_unit_make() 
                                  << " " << sdp_resp.head_unit_model() << std::endl;

                        int video_ch_id = -1;
                        int input_ch_id = -1;
                        
                        // Default projection resolution
                        int res_w = 800;
                        int res_h = 480;

                        for (int i = 0; i < sdp_resp.services_size(); ++i)
                        {
                            const ServiceDescriptor& svc = sdp_resp.services(i);

                            if (svc.has_media_sink_service() && svc.media_sink_service().video_configs_size() > 0)
                            {
                                video_ch_id = svc.service_id();
                                std::cout << "[MAIN] Found Video Sink Service assigned to Channel " << video_ch_id << std::endl;
                            }
                            if (svc.has_input_service())
                            {
                                input_ch_id = svc.service_id();
                                if (svc.input_service().touchscreens_size() > 0) {
                                    res_w = svc.input_service().touchscreens(0).width();
                                    res_h = svc.input_service().touchscreens(0).height();
                                }
                                std::cout << "[MAIN] Found Input Source Service assigned to Channel " << input_ch_id 
                                          << " (Touchscreen Resolution: " << res_w << "x" << res_h << ")" << std::endl;
                            }
                        }

                        // Open the assigned Video Channel (Sent ON the target channel, not channel 0)
                        if (video_ch_id != -1)
                        {
                            ChannelOpenRequest open_req;
                            open_req.set_channel_id(video_ch_id);
                            open_req.set_priority(1);
                            std::string req_str = open_req.SerializeAsString();
                            std::vector<uint8_t> req_pt(req_str.begin(), req_str.end());
                            req_pt.insert(req_pt.begin(), {0x00, 0x07}); // Type 7

                            GalFrame chan_frame;
                            chan_frame.channel_id = video_ch_id; 
                            chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED; // NO Media Flag!
                            chan_frame.payload = tls_ctx.encrypt(req_pt);
                            std::cout << "[MAIN] -> [Ch " << video_ch_id << "] Sending ChannelOpenRequest." << std::endl;
                            usb_transport.write_frame(chan_frame);
                        }

                        // Open the assigned Input Channel
                        if (input_ch_id != -1)
                        {
                            ChannelOpenRequest open_req;
                            open_req.set_channel_id(input_ch_id);
                            open_req.set_priority(2);
                            std::string req_str = open_req.SerializeAsString();
                            std::vector<uint8_t> req_pt(req_str.begin(), req_str.end());
                            req_pt.insert(req_pt.begin(), {0x00, 0x07}); // Type 7

                            GalFrame chan_frame;
                            chan_frame.channel_id = input_ch_id; 
                            chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                            chan_frame.payload = tls_ctx.encrypt(req_pt);
                            std::cout << "[MAIN] -> [Ch " << input_ch_id << "] Sending ChannelOpenRequest." << std::endl;
                            usb_transport.write_frame(chan_frame);
                        }

                        // Request the Head Unit to switch its physical UI over to the projected video feed
                        NavFocusEvent nav;
                        nav.set_focus_state(NAV_FOCUS_PROJECTED);
                        std::string nav_str = nav.SerializeAsString();
                        std::vector<uint8_t> nav_pt(nav_str.begin(), nav_str.end());
                        nav_pt.insert(nav_pt.begin(), {0x00, 0x0E}); // Type 14

                        GalFrame nav_frame;
                        nav_frame.channel_id = 0;
                        nav_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        nav_frame.payload = tls_ctx.encrypt(nav_pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending NavFocusEvent (PROJECTED)." << std::endl;
                        usb_transport.write_frame(nav_frame);

                        // Start hardware capture and the synthetic touch interface
                        if (video_ch_id != -1)
                        {
                            touch.init(res_w, res_h);
                            std::cout << "[MAIN] Launching Hardware Video Capture Thread..." << std::endl;
                            video_thread = std::make_unique<VideoEncoderThread>(usb_transport, tls_ctx, video_ch_id);
                            video_thread->start(res_w, res_h);
                        }
                    }
                    else if (msg_type == 11)
                    { // PingRequest -> PongResponse
                        PingRequest ping;
                        ping.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);

                        PongResponse pong;
                        pong.set_timestamp(ping.timestamp());
                        std::string pong_str = pong.SerializeAsString();
                        std::vector<uint8_t> pong_pt(pong_str.begin(), pong_str.end());
                        pong_pt.insert(pong_pt.begin(), {0x00, 0x0C}); // Type 12

                        GalFrame pong_frame;
                        pong_frame.channel_id = 0;
                        pong_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        pong_frame.payload = tls_ctx.encrypt(pong_pt);
                        usb_transport.write_frame(pong_frame);
                    }
                }
            }
        }
    }
    return 0;
}