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
    for (size_t i = 0; i < std::min(data.size(), (size_t)64); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    if (data.size() > 64) std::cout << "...";
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
        {0x8000, "MediaSetupRequest"}, {0x8001, "MediaStartRequest"}, {0x8003, "MediaConfigResponse/BindingResponse"},
        {0x8008, "VideoFocusNotification"}
    };
    if (types.count(type)) return types[type];
    return "UNKNOWN_TYPE_0x" + (std::stringstream() << std::hex << type).str();
}

int main()
{
    std::signal(SIGINT, signal_handler);
    std::cout << "[MAIN] Starting OpenGAL Linux Emitter..." << std::endl;

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

                if (msg_type == 1) // VersionRequest
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
                        std::cout << "[MAIN-STATE] TLS Handshake Complete! Waiting for AuthComplete..." << std::endl;
                        tls_finished = true;
                    }
                }
                else if (msg_type == 4) // AuthComplete
                {
                    std::cout << "[MAIN-STATE] AuthComplete (Cleartext) received from Phone." << std::endl;
                    auth_complete = true;

                    // As the Head Unit, we MUST reply with AuthComplete in CLEARTEXT.
                    GalFrame auth_frame;
                    auth_frame.channel_id = 0; auth_frame.flags = FLAG_FIRST | FLAG_LAST;
                    auth_frame.payload = {0x00, 0x04, 0x08, 0x00}; // Status = 0
                    
                    std::cout << "[MAIN-TX] Sending AuthComplete (Cleartext)" << std::endl;
                    usb_transport.write_frame(auth_frame);
                }
            }
            // --- ENCRYPTED PROTOCOL PHASE ---
            else if (tls_finished && (frame.flags & FLAG_ENCRYPTED))
            {
                std::vector<uint8_t> plaintext = tls_ctx->decrypt(frame.payload);
                if (plaintext.empty()) continue; 

                // Process Control Messages
                if (frame.channel_id == 0 || (frame.flags & FLAG_CONTROL)) 
                {
                    if (plaintext.size() < 2) continue;
                    uint16_t enc_msg_type = (plaintext[0] << 8) | plaintext[1];
                    std::cout << "[MAIN-RX] Decrypted Control Type on Ch " << (int)frame.channel_id << ": " << decode_msg_type(enc_msg_type) << std::endl;

                    if (enc_msg_type == 0x0005 && auth_complete) // ServiceDiscoveryRequest (from Phone)
                    { 
                        std::cout << "[MAIN-STATE] Phone sent ServiceDiscoveryRequest. Constructing Response..." << std::endl;
                        ServiceDiscovery sdp_resp;
                        sdp_resp.set_head_unit_make("BerryAuto");
                        sdp_resp.set_head_unit_model("Raspberry Pi");
                        sdp_resp.set_head_unit_software_build("1.0");
                        sdp_resp.set_head_unit_software_version("1.0.0");
                        sdp_resp.set_display_resolution(1); // VIDEO_800x480
                        
                        // Setup Video Sink (Channel 2)
                        ServiceDescriptor* video_svc = sdp_resp.add_services();
                        video_svc->set_service_id(2);
                        MediaSinkService* sink = video_svc->mutable_media_sink_service();
                        sink->set_codec_type(MEDIA_CODEC_VIDEO_H264_BP);
                        VideoConfig* vconf = sink->add_video_configs();
                        vconf->set_codec_resolution(VIDEO_800x480);
                        vconf->set_framerate(30);
                        vconf->set_width_margin(0);
                        vconf->set_height_margin(0);
                        vconf->set_density(160);

                        // Setup Touch Input (Channel 3)
                        ServiceDescriptor* input_svc = sdp_resp.add_services();
                        input_svc->set_service_id(3);
                        InputSourceService* input = input_svc->mutable_input_service();
                        TouchscreenConfig* touchscreen = input->add_touchscreens();
                        touchscreen->set_width(800);
                        touchscreen->set_height(480);

                        std::string sdp_resp_str = sdp_resp.SerializeAsString();
                        std::vector<uint8_t> sdp_resp_pt(sdp_resp_str.begin(), sdp_resp_str.end());
                        sdp_resp_pt.insert(sdp_resp_pt.begin(), {0x00, 0x06}); // Type 6 (ServiceDiscoveryResponse)

                        GalFrame sdp_frame;
                        sdp_frame.channel_id = 0;
                        sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        sdp_frame.payload = tls_ctx->encrypt(sdp_resp_pt);
                        
                        std::cout << "[MAIN-TX] Sending ServiceDiscoveryResponse (Encrypted)" << std::endl;
                        usb_transport.write_frame(sdp_frame);
                    }
                    else if (enc_msg_type == 0x0007) // ChannelOpenRequest (from Phone)
                    {
                        std::cout << "[MAIN-STATE] Phone wants to open Channel " << (int)frame.channel_id << std::endl;
                        
                        // Reply with ChannelOpenResponse (Type 8), Status 0
                        std::vector<uint8_t> resp_pt = {0x00, 0x08, 0x08, 0x00}; 
                        GalFrame resp_frame;
                        resp_frame.channel_id = frame.channel_id;
                        resp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                        resp_frame.payload = tls_ctx->encrypt(resp_pt);
                        
                        std::cout << "[MAIN-TX] Sending ChannelOpenResponse on Ch " << (int)frame.channel_id << std::endl;
                        usb_transport.write_frame(resp_frame);
                    }
                    else if (enc_msg_type == 0x8000) // MediaSetupRequest
                    {
                        std::cout << "[MAIN-STATE] Phone sent MediaSetupRequest on Ch " << (int)frame.channel_id << std::endl;
                        
                        // Reply with MediaSetupResponse (0x8003/0x8004 depending on Android Auto version).
                        // Payload: Status = 0, MaxUnacked = 16, config_index = 0
                        std::vector<uint8_t> resp_pt = {0x80, 0x04, 0x08, 0x00, 0x10, 0x10, 0x18, 0x00}; 
                        GalFrame resp_frame;
                        resp_frame.channel_id = frame.channel_id;
                        resp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                        resp_frame.payload = tls_ctx->encrypt(resp_pt);
                        usb_transport.write_frame(resp_frame);
                        std::cout << "[MAIN-TX] Sending MediaSetupResponse on Ch " << (int)frame.channel_id << std::endl;

                        if (frame.channel_id == 2) {
                            // Tell Phone we are ready to receive video projection
                            std::cout << "[MAIN-TX] Sending VideoFocusNotification (PROJECTED)" << std::endl;
                            std::vector<uint8_t> focus_pt = {0x80, 0x08, 0x08, 0x02, 0x10, 0x01}; 
                            GalFrame focus_frame;
                            focus_frame.channel_id = 2;
                            focus_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED | FLAG_CONTROL;
                            focus_frame.payload = tls_ctx->encrypt(focus_pt);
                            usb_transport.write_frame(focus_frame);
                            
                            if (!video_thread) {
                                touch.init(800, 480);
                                std::cout << "[MAIN-STATE] Launching Hardware Video Capture Thread..." << std::endl;
                                video_thread = std::make_unique<VideoEncoderThread>(usb_transport, *tls_ctx, 2);
                                video_thread->start(800, 480);
                            }
                        }
                    }
                    else if (enc_msg_type == 0x000B) // PingRequest
                    { 
                        PingRequest ping;
                        ping.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);

                        PongResponse pong;
                        pong.set_timestamp(ping.timestamp());
                        std::string pong_str = pong.SerializeAsString();
                        std::vector<uint8_t> pong_pt(pong_str.begin(), pong_str.end());
                        pong_pt.insert(pong_pt.begin(), {0x00, 0x0C}); // Type 12

                        GalFrame pong_frame;
                        pong_frame.channel_id = frame.channel_id; 
                        pong_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        pong_frame.payload = tls_ctx->encrypt(pong_pt);
                        usb_transport.write_frame(pong_frame);
                    }
                    else 
                    {
                        hex_dump("[MAIN-WARN] Unhandled Encrypted Packet", plaintext);
                    }
                }
            }
        }
    }
    
    std::cout << "[MAIN] Graceful shutdown initiated..." << std::endl;
    if (video_thread) video_thread->stop();
    return 0;
}