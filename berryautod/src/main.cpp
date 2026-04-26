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
                        AuthComplete auth; auth.set_status(STATUS_SUCCESS);
                        std::string auth_str = auth.SerializeAsString();
                        std::vector<uint8_t> pt(auth_str.begin(), auth_str.end());
                        pt.insert(pt.begin(), {0x00, 0x04});
                        
                        GalFrame auth_frame; auth_frame.channel_id = 0; auth_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        auth_frame.payload = tls_ctx.encrypt(pt);
                        std::cout << "[MAIN] -> [Ch 0] Sending AuthComplete (Encrypted)." << std::endl;
                        usb_transport.write_frame(auth_frame);
                        auth_complete = true;
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
                        if (sdp.display_resolution() == 3) { res_w = 1920; res_h = 1080; }
                        else if (sdp.display_resolution() == 2) { res_w = 1280; res_h = 720; }
                        
                        std::cout << "[MAIN] SDP configures Display Resolution: " << res_w << "x" << res_h << std::endl;
                        touch.init(res_w, res_h);

                        ChannelOpenRequest open_req; open_req.set_channel_id(2); open_req.set_priority(1);
                        std::string req_str = open_req.SerializeAsString();
                        std::vector<uint8_t> req_pt(req_str.begin(), req_str.end());
                        req_pt.insert(req_pt.begin(), {0x00, 0x07});
                        
                        GalFrame chan_frame; chan_frame.channel_id = 2; 
                        chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        chan_frame.payload = tls_ctx.encrypt(req_pt);
                        std::cout << "[MAIN] -> [Ch 2] Sending ChannelOpenRequest." << std::endl;
                        usb_transport.write_frame(chan_frame);

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
                        std::cout << "[MAIN] <- [Ch 0] Received PingRequest (timestamp: " << ping.timestamp() << ")." << std::endl;
                        
                        PongResponse pong; pong.set_timestamp(ping.timestamp());
                        std::string pong_str = pong.SerializeAsString();
                        std::vector<uint8_t> pong_pt(pong_str.begin(), pong_str.end());
                        pong_pt.insert(pong_pt.begin(), {0x00, 0x0C});
                        
                        GalFrame pong_frame; pong_frame.channel_id = 0;
                        pong_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        pong_frame.payload = tls_ctx.encrypt(pong_pt);
                        // Commenting out the Pong send log to avoid spamming the console 1x per second
                        // std::cout << "[MAIN] -> [Ch 0] Sending PongResponse." << std::endl;
                        usb_transport.write_frame(pong_frame);
                    }
                    else {
                        std::cout << "[MAIN] <- [Ch 0] Received Unknown/Unhandled Message Type: " << msg_type << std::endl;
                    }
                }
            }
        }
    }
    return 0;
}