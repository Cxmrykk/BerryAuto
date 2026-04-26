#include "crypto_tls.h"
#include "input_injector.h"
#include "opengal.pb.h"
#include "transport_ffs.h"
#include "video_encoder.h"
#include <iomanip>
#include <iostream>
#include <map>
#include <csignal>
#include <atomic>

using namespace opengal;

std::atomic<bool> global_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        global_running = false;
    }
}

void hex_dump(const std::string& prefix, const std::vector<uint8_t>& data) {
    std::cout << prefix << " [" << data.size() << " bytes]: ";
    for (size_t i = 0; i < std::min(data.size(), (size_t)48); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    if (data.size() > 48) std::cout << "...";
    std::cout << std::dec << std::endl;
}

std::string decode_flags(uint8_t flags) {
    std::string s = "[";
    if (flags & FLAG_FIRST) s += "FIRST|";
    if (flags & FLAG_LAST) s += "LAST|";
    if (flags & FLAG_CONTROL) s += "CONTROL|";
    if (flags & FLAG_ENCRYPTED) s += "ENCRYPTED|";
    if (s.back() == '|') s.pop_back();
    s += "]";
    return s;
}

std::string decode_msg_type(uint16_t type) {
    static std::map<uint16_t, std::string> types = {
        {0x0001, "VersionRequest"}, {0x0002, "VersionResponse"}, {0x0003, "SslHandshake"},
        {0x0004, "AuthComplete"}, {0x0005, "ServiceDiscoveryRequest"}, {0x0006, "ServiceDiscoveryResponse"}, 
        {0x0007, "ChannelOpenRequest"}, {0x0008, "ChannelOpenResponse"},
        {0x000B, "PingRequest"}, {0x000C, "PongResponse"}, {0x000E, "NavFocusEvent"},
        {0x0018, "CallStatus"},
        {0x8000, "MediaSetupRequest"}, {0x8001, "MediaStartRequest"}, 
        {0x8003, "MediaConfigResponse"}, {0x8004, "MediaAck"}, 
        {0x8008, "VideoFocusNotification"}
    };
    if (types.count(type)) return types[type];
    return "UNKNOWN_TYPE_0x" + (std::stringstream() << std::hex << type).str();
}

// Helper to prepend the 16-bit Type ID to a serialized Protobuf payload
std::vector<uint8_t> wrap_protobuf(uint16_t msg_type, const std::string& serialized_proto) {
    std::vector<uint8_t> pt;
    pt.push_back((msg_type >> 8) & 0xFF);
    pt.push_back(msg_type & 0xFF);
    pt.insert(pt.end(), serialized_proto.begin(), serialized_proto.end());
    return pt;
}

int main()
{
    std::signal(SIGINT, signal_handler);
    std::cout << "[MAIN] Starting OpenGAL Linux Emitter (Phone Role)..." << std::endl;

    FunctionFSTransport usb_transport("/dev/ffs-opengal");
    if (!usb_transport.init()) return 1;

    std::unique_ptr<OpenGALTlsContext> tls_ctx = std::make_unique<OpenGALTlsContext>();
    std::unique_ptr<VideoEncoderThread> video_thread;
    InputInjector touch;

    bool tls_finished = false;
    bool auth_complete = false;

    std::cout << "[MAIN] Listening for frames..." << std::endl;

    while (global_running && usb_transport.is_running())
    {
        auto frames = usb_transport.read_frames();
        for (const auto& frame : frames)
        {
            // Hide the massive flood of Video frames, print everything else
            if (!(frame.flags & FLAG_ENCRYPTED) || (frame.flags & FLAG_CONTROL) || frame.channel_id == 0) {
                std::cout << "\n[MAIN-RX] Frame: Ch=" << (int)frame.channel_id 
                          << " Flags=" << decode_flags(frame.flags) 
                          << " Len=" << frame.payload.size() << std::endl;
            }

            // --- CLEARTEXT PROTOCOL PHASE ---
            if (!(frame.flags & FLAG_ENCRYPTED)) 
            {
                if (frame.payload.size() < 2) continue;

                uint16_t msg_type = (frame.payload[0] << 8) | frame.payload[1];
                std::cout << "[MAIN-RX] Decoded Cleartext Type on Ch 0: " << decode_msg_type(msg_type) << std::endl;

                if (msg_type == 1) // VersionRequest (from Head Unit)
                { 
                    std::cout << "[MAIN-STATE] Resetting Session State..." << std::endl;
                    tls_finished = false; auth_complete = false;
                    tls_ctx = std::make_unique<OpenGALTlsContext>();
                    if (video_thread) { video_thread->stop(); video_thread.reset(); }

                    GalFrame resp;
                    resp.channel_id = 0; resp.flags = FLAG_FIRST | FLAG_LAST;
                    resp.payload = {0x00, 0x02, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00};
                    usb_transport.write_frame(resp);
                }
                else if (msg_type == 3) // SslHandshake
                { 
                    std::vector<uint8_t> tls_input(frame.payload.begin() + 2, frame.payload.end());
                    std::vector<uint8_t> tls_output;
                    bool is_finished = tls_ctx->do_handshake(tls_input, tls_output);

                    if (!tls_output.empty()) {
                        GalFrame resp;
                        resp.channel_id = 0; resp.flags = FLAG_FIRST | FLAG_LAST;
                        resp.payload = {0x00, 0x03};
                        resp.payload.insert(resp.payload.end(), tls_output.begin(), tls_output.end());
                        usb_transport.write_frame(resp);
                    }

                    if (is_finished && !tls_finished) {
                        std::cout << "[MAIN-STATE] TLS Handshake Complete! Waiting for AuthComplete from HU..." << std::endl;
                        tls_finished = true;
                    }
                }
                else if (msg_type == 4) // AuthComplete (from Head Unit)
                {
                    std::cout << "[MAIN-STATE] AuthComplete received. The Head Unit trusts us." << std::endl;
                    auth_complete = true;

                    // We do NOT reply with AuthComplete.
                    // We immediately send ServiceDiscoveryRequest (Encrypted).
                    ServiceDiscoveryRequest sdp_req;
                    sdp_req.set_phone_name("BerryAuto Emitter");
                    
                    GalFrame sdp_frame;
                    sdp_frame.channel_id = 0;
                    sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                    sdp_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x0005, sdp_req.SerializeAsString()));
                    
                    std::cout << "[MAIN-TX] Sending ServiceDiscoveryRequest (Type 5)" << std::endl;
                    hex_dump("[TX-DUMP]", sdp_frame.payload);
                    usb_transport.write_frame(sdp_frame);
                }
            }
            // --- ENCRYPTED PROTOCOL PHASE ---
            else if (tls_finished && (frame.flags & FLAG_ENCRYPTED))
            {
                std::vector<uint8_t> plaintext = tls_ctx->decrypt(frame.payload);
                if (plaintext.empty()) continue; 

                // Process Control Messages (Either on Ch 0, or on Media channels with FLAG_CONTROL)
                if (frame.channel_id == 0 || (frame.flags & FLAG_CONTROL)) 
                {
                    if (plaintext.size() < 2) continue;
                    uint16_t enc_msg_type = (plaintext[0] << 8) | plaintext[1];
                    
                    if (enc_msg_type != 0x000B && enc_msg_type != 0x000C) { // Hide Pings to keep logs clean
                        std::cout << "[MAIN-RX] Decrypted Control Type on Ch " << (int)frame.channel_id << ": " << decode_msg_type(enc_msg_type) << std::endl;
                        hex_dump("[RX-DUMP]", plaintext);
                    }

                    if (enc_msg_type == 0x0006 && auth_complete) // ServiceDiscoveryResponse (from Head Unit)
                    { 
                        ServiceDiscovery sdp_resp;
                        sdp_resp.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);
                        std::cout << "[MAIN-SDP] Head Unit Info: " << sdp_resp.head_unit_make() << " " << sdp_resp.head_unit_model() << std::endl;

                        // Open Channel 2 for Video
                        ChannelOpenRequest open_req;
                        open_req.set_channel_id(2);
                        open_req.set_priority(1);

                        GalFrame chan_frame;
                        chan_frame.channel_id = 2; // Sent ON the channel we want to open
                        chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                        chan_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x0007, open_req.SerializeAsString()));
                        
                        std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch 2" << std::endl;
                        usb_transport.write_frame(chan_frame);
                        
                        // Open Channel 3 for Touch Input
                        open_req.set_channel_id(3);
                        open_req.set_priority(2);
                        chan_frame.channel_id = 3; 
                        chan_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x0007, open_req.SerializeAsString()));
                        std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch 3" << std::endl;
                        usb_transport.write_frame(chan_frame);
                    }
                    else if (enc_msg_type == 0x0008) // ChannelOpenResponse
                    {
                        std::cout << "[MAIN-STATE] Channel " << (int)frame.channel_id << " Opened Successfully!" << std::endl;
                        if (frame.channel_id == 2) {
                            // Request Media Setup
                            MediaSetupRequest setup_req;
                            setup_req.set_type(0);
                            
                            GalFrame setup_frame;
                            setup_frame.channel_id = 2;
                            setup_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                            setup_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x8000, setup_req.SerializeAsString()));
                            
                            std::cout << "[MAIN-TX] Sending MediaSetupRequest on Ch 2" << std::endl;
                            usb_transport.write_frame(setup_frame);
                        }
                    }
                    else if (enc_msg_type == 0x8003) // MediaConfigResponse
                    {
                        std::cout << "[MAIN-STATE] Received MediaConfigResponse on Ch " << (int)frame.channel_id << std::endl;
                        if (frame.channel_id == 2) {
                            
                            // Start the Media Stream
                            MediaStart start_req;
                            start_req.set_session_id(1); 
                            start_req.set_configuration_index(0); 

                            GalFrame start_frame;
                            start_frame.channel_id = 2;
                            start_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                            start_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x8001, start_req.SerializeAsString()));
                            
                            std::cout << "[MAIN-TX] Sending MediaStartRequest on Ch 2" << std::endl;
                            usb_transport.write_frame(start_frame);

                            // Force the Head Unit to switch to our projection screen
                            NavFocusEvent nav;
                            nav.set_focus_state(NAV_FOCUS_PROJECTED);

                            GalFrame nav_frame;
                            nav_frame.channel_id = 0; // Nav Focus goes over Ch 0
                            nav_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                            nav_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x000E, nav.SerializeAsString()));
                            
                            std::cout << "[MAIN-TX] Requesting Screen Focus (NavFocusEvent)..." << std::endl;
                            usb_transport.write_frame(nav_frame);
                        }
                    }
                    else if (enc_msg_type == 0x8008) // VideoFocusNotification
                    {
                        std::cout << "[MAIN-STATE] Screen Focus Granted! Car is ready for Video." << std::endl;
                        if (!video_thread) {
                            std::cout << "[MAIN-STATE] Launching Hardware Video Capture Thread..." << std::endl;
                            touch.init(800, 480);
                            video_thread = std::make_unique<VideoEncoderThread>(usb_transport, *tls_ctx, 2);
                            video_thread->start(800, 480);
                        }
                    }
                    else if (enc_msg_type == 0x000B) // PingRequest
                    { 
                        PingRequest ping;
                        ping.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);

                        PongResponse pong;
                        pong.set_timestamp(ping.timestamp());

                        GalFrame pong_frame;
                        pong_frame.channel_id = frame.channel_id; 
                        pong_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        pong_frame.payload = tls_ctx->encrypt(wrap_protobuf(0x000C, pong.SerializeAsString()));
                        usb_transport.write_frame(pong_frame);
                    }
                }
            }
        }
    }
    
    std::cout << "[MAIN] Graceful shutdown initiated..." << std::endl;
    if (video_thread) video_thread->stop();
    return 0;
}