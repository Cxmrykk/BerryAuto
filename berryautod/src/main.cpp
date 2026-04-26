#include "transport_ffs.h"
#include "crypto_tls.h"
#include "video_encoder.h"
#include "input_injector.h"
#include "opengal.pb.h"
#include <iostream>
#include <iomanip>

using namespace opengal;

int main() {
    std::cout << "[MAIN] Starting OpenGAL Linux Emitter..." << std::endl;
    
    FunctionFSTransport usb_transport("/dev/ffs-opengal");
    if (!usb_transport.init()) return 1;

    OpenGALTlsContext tls_ctx;
    VideoEncoderThread video_thread(usb_transport, tls_ctx, 2); 
    InputInjector touch;

    bool auth_complete = false;

    std::cout << "[MAIN] Listening for frames..." << std::endl;

    while (true) {
        auto frames = usb_transport.read_frames();
        for (const auto& frame : frames) {
            
            if (frame.channel_id == 0 && !auth_complete) {
                uint16_t msg_type = (frame.payload[0] << 8) | frame.payload[1];
                
                if (msg_type == 1) { // VersionRequest -> VersionResponse
                    std::cout << "[MAIN] <- [Ch 0] Received VersionRequest." << std::endl;
                    GalFrame resp; resp.channel_id = 0; resp.flags = FLAG_FIRST | FLAG_LAST;
                    resp.payload = {0x00, 0x02, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00};
                    std::cout << "[MAIN] -> [Ch 0] Sending VersionResponse (Major: 1, Minor: 6)." << std::endl;
                    usb_transport.write_frame(resp);
                } 
                else if (msg_type == 3) { // SslHandshake
                    std::cout << "[MAIN] <- [Ch 0] Received SslHandshake Record (" << frame.payload.size() - 2 << " bytes)." << std::endl;
                    std::vector<uint8_t> tls_input(frame.payload.begin() + 2, frame.payload.end());
                    std::vector<uint8_t> tls_output;
                    bool is_finished = tls_ctx.do_handshake(tls_input, tls_output);
                    
                    if (!tls_output.empty()) {
                        std::cout << "[MAIN] -> [Ch 0] Sending SslHandshake Reply (" << tls_output.size() << " bytes)." << std::endl;
                        GalFrame resp; resp.channel_id = 0; resp.flags = FLAG_FIRST | FLAG_LAST;
                        resp.payload.push_back(0x00); resp.payload.push_back(0x03); 
                        resp.payload.insert(resp.payload.end(), tls_output.begin(), tls_output.end());
                        usb_transport.write_frame(resp);
                    }

                    if (is_finished) {
                        std::cout << "[MAIN] *** TLS Handshake Complete! Transitioning to Encrypted Mode. ***" << std::endl;
                        
                        // 1. Send AuthComplete
                        AuthComplete auth; auth.set_status(STATUS_SUCCESS);
                        std::string auth_str = auth.SerializeAsString();
                        std::vector<uint8_t> pt(auth_str.begin(), auth_str.end());
                        pt.insert(pt.begin(), {0x00, 0x04});
                        
                        GalFrame auth_frame; auth_frame.channel_id = 0; auth_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        auth_frame.payload = tls_ctx.encrypt(pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending AuthComplete (Encrypted)." << std::endl;
                        usb_transport.write_frame(auth_frame);
                        auth_complete = true;

                        // 2. Send Service Discovery Request (Declare our Hardware Capabilities)
                        ServiceDiscovery sdp_req;
                        sdp_req.set_head_unit_make("OpenGAL");
                        sdp_req.set_head_unit_model("BerryAuto");
                        sdp_req.set_head_unit_software_build("1.0");
                        sdp_req.set_head_unit_software_version("1.0");

                        // Add Video Service (Channel 2)
                        ServiceDescriptor* video_svc = sdp_req.add_services();
                        video_svc->set_service_id(2);
                        MediaSinkService* media_sink = new MediaSinkService();
                        media_sink->set_codec_type(MEDIA_CODEC_VIDEO_H264_BP);
                        VideoConfig* video_config = media_sink->add_video_configs();
                        video_config->set_codec_resolution(VIDEO_800x480);
                        video_config->set_framerate(30);
                        video_config->set_width_margin(0);
                        video_config->set_height_margin(0);
                        video_config->set_density(160);
                        video_svc->set_allocated_media_sink_service(media_sink);

                        // Add Touch Input Service (Channel 3)
                        ServiceDescriptor* input_svc = sdp_req.add_services();
                        input_svc->set_service_id(3);
                        InputSourceService* input_src = new InputSourceService();
                        TouchscreenConfig* touch_cfg = input_src->add_touchscreens();
                        touch_cfg->set_width(800);
                        touch_cfg->set_height(480);
                        input_svc->set_allocated_input_service(input_src);

                        // Add Audio Sink Service (Channel 4) - Required by phone to route music/nav
                        ServiceDescriptor* audio_sink_svc = sdp_req.add_services();
                        audio_sink_svc->set_service_id(4);
                        MediaSinkService* a_sink = new MediaSinkService();
                        a_sink->set_codec_type(MEDIA_CODEC_AUDIO_PCM);
                        a_sink->set_audio_stream_type(1); // Media Stream
                        // Omit AudioConfig allocation as the phone will accept PCM default constraints
                        audio_sink_svc->set_allocated_media_sink_service(a_sink);

                        std::string sdp_str = sdp_req.SerializeAsString();
                        std::vector<uint8_t> sdp_pt(sdp_str.begin(), sdp_str.end());
                        sdp_pt.insert(sdp_pt.begin(), {0x00, 0x06});

                        GalFrame sdp_frame; sdp_frame.channel_id = 0; sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        sdp_frame.payload = tls_ctx.encrypt(sdp_pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending ServiceDiscoveryRequest (Video, Input, Audio)." << std::endl;
                        usb_transport.write_frame(sdp_frame);
                    }
                }
            } 
            else if (auth_complete && (frame.flags & FLAG_ENCRYPTED)) {
                std::vector<uint8_t> plaintext = tls_ctx.decrypt(frame.payload);
                if (plaintext.size() < 2) continue;

                uint16_t msg_type = (plaintext[0] << 8) | plaintext[1];

                if (frame.channel_id == 0) {
                    if (msg_type == 6) { // ServiceDiscoveryResponse
                        std::cout << "[MAIN] <- [Ch 0] Received ServiceDiscoveryResponse." << std::endl;
                        ServiceDiscovery sdp;
                        sdp.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);
                        
                        int res_w = 800, res_h = 480;
                        touch.init(res_w, res_h);

                        // Open Video Channel (2)
                        ChannelOpenRequest open_req; open_req.set_channel_id(2); open_req.set_priority(1);
                        std::string req_str = open_req.SerializeAsString();
                        std::vector<uint8_t> req_pt(req_str.begin(), req_str.end());
                        req_pt.insert(req_pt.begin(), {0x00, 0x07});
                        
                        GalFrame chan_frame; chan_frame.channel_id = 2; // Sent on the target channel
                        chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        chan_frame.payload = tls_ctx.encrypt(req_pt);
                        std::cout << "[MAIN] -> [Ch 2] Sending ChannelOpenRequest." << std::endl;
                        usb_transport.write_frame(chan_frame);

                        // Open Input Channel (3)
                        open_req.set_channel_id(3); open_req.set_priority(2);
                        req_str = open_req.SerializeAsString();
                        req_pt.assign(req_str.begin(), req_str.end());
                        req_pt.insert(req_pt.begin(), {0x00, 0x07});
                        chan_frame.channel_id = 3;
                        chan_frame.payload = tls_ctx.encrypt(req_pt);
                        std::cout << "[MAIN] -> [Ch 3] Sending ChannelOpenRequest." << std::endl;
                        usb_transport.write_frame(chan_frame);

                        // Request the Phone to project UI to the Native display
                        NavFocusEvent nav; nav.set_focus_state(NAV_FOCUS_PROJECTED);
                        std::string nav_str = nav.SerializeAsString();
                        std::vector<uint8_t> nav_pt(nav_str.begin(), nav_str.end());
                        nav_pt.insert(nav_pt.begin(), {0x00, 0x0E});

                        GalFrame nav_frame; nav_frame.channel_id = 0;
                        nav_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        nav_frame.payload = tls_ctx.encrypt(nav_pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending NavFocusEvent (PROJECTED)." << std::endl;
                        usb_transport.write_frame(nav_frame);

                        std::cout << "[MAIN] Launching Hardware Capture Video Thread..." << std::endl;
                        video_thread.start(res_w, res_h);
                    }
                    else if (msg_type == 11) { // PingRequest -> PongResponse
                        PingRequest ping; ping.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);
                        // std::cout << "[MAIN] <- [Ch 0] Received PingRequest (timestamp: " << ping.timestamp() << ")." << std::endl;
                        
                        PongResponse pong; pong.set_timestamp(ping.timestamp());
                        std::string pong_str = pong.SerializeAsString();
                        std::vector<uint8_t> pong_pt(pong_str.begin(), pong_str.end());
                        pong_pt.insert(pong_pt.begin(), {0x00, 0x0C});
                        
                        GalFrame pong_frame; pong_frame.channel_id = 0;
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