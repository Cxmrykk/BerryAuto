#include "crypto_tls.h"
#include "input_injector.h"
#include "opengal.pb.h"
#include "transport_ffs.h"
#include "video_encoder.h"
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>

using namespace opengal;

// --- Debugging Helpers ---
void hex_dump(const std::string& prefix, const std::vector<uint8_t>& data) {
    std::cout << prefix << " [" << data.size() << " bytes]: ";
    for (size_t i = 0; i < std::min(data.size(), (size_t)32); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    if (data.size() > 32) std::cout << "...";
    std::cout << std::dec << std::endl;
}

std::string decode_flags(uint8_t flags) {
    std::string s = "[";
    if (flags & FLAG_FIRST) s += "FIRST|";
    if (flags & FLAG_LAST) s += "LAST|";
    if (flags & FLAG_MEDIA) s += "MEDIA|";
    if (flags & FLAG_ENCRYPTED) s += "ENCRYPTED|";
    if (s.back() == '|') s.pop_back();
    s += "]";
    return s;
}

std::string decode_msg_type(uint16_t type) {
    static std::map<uint16_t, std::string> types = {
        {1, "VersionRequest"}, {2, "VersionResponse"}, {3, "SslHandshake"},
        {4, "AuthComplete"}, {6, "ServiceDiscovery"}, {7, "ChannelOpenRequest"},
        {11, "PingRequest"}, {12, "PongResponse"}, {14, "NavFocusEvent"},
        {15, "ByeByeRequest"}, {16, "ByeByeResponse"}, {19, "AudioFocusState"},
        {24, "CallStatus"}, {26, "UpdateService"}
    };
    if (types.count(type)) return types[type];
    return "UNKNOWN_TYPE_" + std::to_string(type);
}
// -------------------------

int main()
{
    std::cout << "[MAIN] Starting OpenGAL Linux Emitter with EXTRA DEBUGGING..." << std::endl;

    FunctionFSTransport usb_transport("/dev/ffs-opengal");
    if (!usb_transport.init()) return 1;

    std::unique_ptr<OpenGALTlsContext> tls_ctx = std::make_unique<OpenGALTlsContext>();
    std::unique_ptr<VideoEncoderThread> video_thread;
    InputInjector touch;

    bool tls_finished = false;
    bool auth_complete = false;

    std::cout << "[MAIN] Listening for frames..." << std::endl;

    while (usb_transport.is_running())
    {
        auto frames = usb_transport.read_frames();
        for (const auto& frame : frames)
        {
            // Only aggressively log Channel 0 to avoid Video/Audio spam
            if (frame.channel_id == 0) {
                std::cout << "\n[MAIN-RX] Frame Received: Ch=0 Flags=" << decode_flags(frame.flags) 
                          << " Len=" << frame.payload.size() << std::endl;
            }

            if (frame.channel_id == 0)
            {
                // --- CLEARTEXT MESSAGES ---
                if (!(frame.flags & FLAG_ENCRYPTED)) 
                {
                    if (frame.payload.size() < 2) {
                        std::cout << "[MAIN-WARN] Cleartext payload too small!" << std::endl;
                        continue;
                    }
                    uint16_t msg_type = (frame.payload[0] << 8) | frame.payload[1];
                    std::cout << "[MAIN-RX] Decoded Type: " << decode_msg_type(msg_type) << std::endl;

                    if (msg_type == 1) // VersionRequest
                    { 
                        std::cout << "[MAIN] *** RESETTING SESSION STATE (VersionRequest Received) ***" << std::endl;
                        tls_finished = false;
                        auth_complete = false;
                        tls_ctx = std::make_unique<OpenGALTlsContext>();
                        if (video_thread) {
                            video_thread->stop();
                            video_thread.reset();
                        }

                        GalFrame resp;
                        resp.channel_id = 0;
                        resp.flags = FLAG_FIRST | FLAG_LAST;
                        resp.payload = {0x00, 0x02, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00};
                        
                        std::cout << "[MAIN-TX] Sending VersionResponse (Major: 1, Minor: 6)" << std::endl;
                        usb_transport.write_frame(resp);
                    }
                    else if (msg_type == 3 && !tls_finished) // SslHandshake
                    { 
                        hex_dump("[MAIN-TLS] Handshake Record In", frame.payload);
                        std::vector<uint8_t> tls_input(frame.payload.begin() + 2, frame.payload.end());
                        std::vector<uint8_t> tls_output;
                        
                        bool is_finished = tls_ctx->do_handshake(tls_input, tls_output);

                        if (!tls_output.empty())
                        {
                            GalFrame resp;
                            resp.channel_id = 0;
                            resp.flags = FLAG_FIRST | FLAG_LAST;
                            resp.payload.push_back(0x00);
                            resp.payload.push_back(0x03);
                            resp.payload.insert(resp.payload.end(), tls_output.begin(), tls_output.end());
                            
                            std::cout << "[MAIN-TX] Sending SslHandshake Reply (" << tls_output.size() << " bytes)" << std::endl;
                            usb_transport.write_frame(resp);
                        }

                        if (is_finished)
                        {
                            std::cout << "[MAIN-STATE] *** TLS Handshake Complete! Waiting for Car AuthComplete... ***" << std::endl;
                            tls_finished = true;
                        }
                    }
                }
                // --- ENCRYPTED MESSAGES ---
                else if (tls_finished && (frame.flags & FLAG_ENCRYPTED))
                {
                    std::vector<uint8_t> plaintext = tls_ctx->decrypt(frame.payload);
                    if (plaintext.size() < 2) {
                        std::cout << "[MAIN-WARN] Decrypted payload too small or empty!" << std::endl;
                        continue;
                    }

                    uint16_t enc_msg_type = (plaintext[0] << 8) | plaintext[1];
                    std::cout << "[MAIN-RX] Decrypted Type: " << decode_msg_type(enc_msg_type) << std::endl;

                    if (enc_msg_type == 4) // AuthComplete (From Head Unit)
                    {
                        std::cout << "[MAIN-STATE] AuthComplete received from Head Unit. We are Trusted." << std::endl;
                        auth_complete = true;

                        ServiceDiscovery sdp_req;
                        std::string sdp_req_str = sdp_req.SerializeAsString();
                        std::vector<uint8_t> sdp_req_pt(sdp_req_str.begin(), sdp_req_str.end());
                        sdp_req_pt.insert(sdp_req_pt.begin(), {0x00, 0x06}); // Type 6

                        GalFrame sdp_frame;
                        sdp_frame.channel_id = 0;
                        sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        sdp_frame.payload = tls_ctx->encrypt(sdp_req_pt);
                        
                        std::cout << "[MAIN-TX] Sending ServiceDiscoveryRequest" << std::endl;
                        usb_transport.write_frame(sdp_frame);
                    }
                    else if (enc_msg_type == 6 && auth_complete) // ServiceDiscoveryResponse (From Head Unit)
                    { 
                        ServiceDiscovery sdp_resp;
                        sdp_resp.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);

                        std::cout << "[MAIN-SDP] Connected to Head Unit: " << sdp_resp.head_unit_make() 
                                  << " " << sdp_resp.head_unit_model() << std::endl;

                        int video_ch_id = -1;
                        int input_ch_id = -1;
                        int res_w = 800, res_h = 480;

                        for (int i = 0; i < sdp_resp.services_size(); ++i)
                        {
                            const ServiceDescriptor& svc = sdp_resp.services(i);
                            if (svc.has_media_sink_service() && svc.media_sink_service().video_configs_size() > 0)
                            {
                                video_ch_id = svc.service_id();
                                std::cout << "[MAIN-SDP] Found Video Sink Service (Ch " << video_ch_id << ")" << std::endl;
                            }
                            if (svc.has_input_service())
                            {
                                input_ch_id = svc.service_id();
                                if (svc.input_service().touchscreens_size() > 0) {
                                    res_w = svc.input_service().touchscreens(0).width();
                                    res_h = svc.input_service().touchscreens(0).height();
                                }
                                std::cout << "[MAIN-SDP] Found Input Source Service (Ch " << input_ch_id 
                                          << ") Res: " << res_w << "x" << res_h << std::endl;
                            }
                        }

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
                            chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED; 
                            chan_frame.payload = tls_ctx->encrypt(req_pt);
                            std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch " << video_ch_id << std::endl;
                            usb_transport.write_frame(chan_frame);
                        }

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
                            chan_frame.payload = tls_ctx->encrypt(req_pt);
                            std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch " << input_ch_id << std::endl;
                            usb_transport.write_frame(chan_frame);
                        }

                        NavFocusEvent nav;
                        nav.set_focus_state(NAV_FOCUS_PROJECTED);
                        std::string nav_str = nav.SerializeAsString();
                        std::vector<uint8_t> nav_pt(nav_str.begin(), nav_str.end());
                        nav_pt.insert(nav_pt.begin(), {0x00, 0x0E}); // Type 14

                        GalFrame nav_frame;
                        nav_frame.channel_id = 0;
                        nav_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        nav_frame.payload = tls_ctx->encrypt(nav_pt);
                        std::cout << "[MAIN-TX] Sending NavFocusEvent (PROJECTED)" << std::endl;
                        usb_transport.write_frame(nav_frame);

                        if (video_ch_id != -1)
                        {
                            touch.init(res_w, res_h);
                            std::cout << "[MAIN-STATE] Launching Hardware Video Capture Thread..." << std::endl;
                            video_thread = std::make_unique<VideoEncoderThread>(usb_transport, *tls_ctx, video_ch_id);
                            video_thread->start(res_w, res_h);
                        }
                    }
                    else if (enc_msg_type == 11) // PingRequest -> PongResponse
                    { 
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
                        pong_frame.payload = tls_ctx->encrypt(pong_pt);
                        
                        // We comment this out normally, but keep it for extreme debugging if needed.
                        std::cout << "[MAIN-TX] Pong!" << std::endl;
                        usb_transport.write_frame(pong_frame);
                    }
                    else 
                    {
                        std::cout << "[MAIN-WARN] Unhandled Encrypted Message Type: " << enc_msg_type << std::endl;
                    }
                }
            }
        }
    }
    
    std::cout << "[MAIN] Exiting..." << std::endl;
    return 0;
}