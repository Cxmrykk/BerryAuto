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
        {0x0001, "VersionRequest"}, {0x0002, "VersionResponse"}, {0x0003, "SslHandshake"},
        {0x0004, "AuthComplete"}, {0x0006, "ServiceDiscovery"}, {0x0007, "ChannelOpenRequest"},
        {0x000B, "PingRequest"}, {0x000C, "PongResponse"}, {0x000E, "NavFocusEvent"},
        {0x0018, "CallStatus"},
        {0x8001, "MediaSetupRequest"}, {0x8002, "MediaStartRequest"}, {0x8003, "MediaStopRequest"},
        {0x8004, "MediaSetupResponse"}, {0x8008, "VideoFocusNotification"}
    };
    if (types.count(type)) return types[type];
    return "UNKNOWN_TYPE_" + std::to_string(type);
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
    int video_ch = -1;
    int input_ch = -1;

    std::cout << "[MAIN] Listening for frames..." << std::endl;

    while (global_running && usb_transport.is_running())
    {
        auto frames = usb_transport.read_frames();
        for (const auto& frame : frames)
        {
            // Only print headers for Media frames to prevent log spam
            if (!(frame.flags & FLAG_MEDIA)) {
                std::cout << "\n[MAIN-RX] Frame Received: Ch=" << (int)frame.channel_id 
                          << " Flags=" << decode_flags(frame.flags) 
                          << " Len=" << frame.payload.size() << std::endl;
            }

            // --- CLEARTEXT MESSAGES ---
            if (!(frame.flags & FLAG_ENCRYPTED)) 
            {
                if (frame.channel_id != 0 || frame.payload.size() < 2) continue;

                uint16_t msg_type = (frame.payload[0] << 8) | frame.payload[1];
                std::cout << "[MAIN-RX] Decoded Cleartext Type on Ch 0: " << decode_msg_type(msg_type) << std::endl;

                if (msg_type == 1) // VersionRequest
                { 
                    std::cout << "[MAIN] *** RESETTING SESSION STATE ***" << std::endl;
                    tls_finished = false;
                    auth_complete = false;
                    video_ch = -1;
                    input_ch = -1;
                    
                    tls_ctx = std::make_unique<OpenGALTlsContext>();
                    if (video_thread) {
                        video_thread->stop();
                        video_thread.reset();
                    }

                    GalFrame resp;
                    resp.channel_id = 0;
                    resp.flags = FLAG_FIRST | FLAG_LAST;
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
                        resp.channel_id = 0;
                        resp.flags = FLAG_FIRST | FLAG_LAST;
                        resp.payload.push_back(0x00);
                        resp.payload.push_back(0x03);
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
                    std::cout << "[MAIN-STATE] AuthComplete (Cleartext) received. We are Trusted." << std::endl;
                    auth_complete = true;

                    GalFrame auth_frame;
                    auth_frame.channel_id = 0;
                    auth_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                    std::vector<uint8_t> auth_pt = {0x00, 0x04, 0x08, 0x00}; 
                    auth_frame.payload = tls_ctx->encrypt(auth_pt);
                    std::cout << "[MAIN-TX] Sending AuthComplete (Encrypted)" << std::endl;
                    usb_transport.write_frame(auth_frame);

                    video_ch = 2;
                    input_ch = 3;

                    ServiceDiscovery sdp_req;
                    ServiceDescriptor* video_svc = sdp_req.add_services();
                    video_svc->set_service_id(video_ch);
                    MediaSinkService* sink = video_svc->mutable_media_sink_service();
                    sink->set_codec_type(MEDIA_CODEC_VIDEO_H264_BP);
                    VideoConfig* vconf = sink->add_video_configs();
                    vconf->set_codec_resolution(VIDEO_800x480);
                    vconf->set_framerate(30);

                    ServiceDescriptor* input_svc = sdp_req.add_services();
                    input_svc->set_service_id(input_ch);
                    InputSourceService* input = input_svc->mutable_input_service();
                    TouchscreenConfig* touchscreen = input->add_touchscreens();
                    touchscreen->set_width(800);
                    touchscreen->set_height(480);

                    std::string sdp_req_str = sdp_req.SerializeAsString();
                    std::vector<uint8_t> sdp_req_pt(sdp_req_str.begin(), sdp_req_str.end());
                    sdp_req_pt.insert(sdp_req_pt.begin(), {0x00, 0x06}); // Type 6

                    GalFrame sdp_frame;
                    sdp_frame.channel_id = 0;
                    sdp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                    sdp_frame.payload = tls_ctx->encrypt(sdp_req_pt);
                    std::cout << "[MAIN-TX] Sending ServiceDiscoveryRequest (Encrypted)" << std::endl;
                    usb_transport.write_frame(sdp_frame);

                    // Force Open Channels IMMEDIATELY
                    ChannelOpenRequest open_req;
                    open_req.set_channel_id(video_ch);
                    open_req.set_priority(1);
                    std::string req_str = open_req.SerializeAsString();
                    std::vector<uint8_t> req_pt(req_str.begin(), req_str.end());
                    req_pt.insert(req_pt.begin(), {0x00, 0x07}); // Type 7

                    GalFrame chan_frame;
                    chan_frame.channel_id = video_ch; 
                    chan_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED; 
                    chan_frame.payload = tls_ctx->encrypt(req_pt);
                    std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch " << video_ch << std::endl;
                    usb_transport.write_frame(chan_frame);

                    open_req.set_channel_id(input_ch);
                    open_req.set_priority(2);
                    req_str = open_req.SerializeAsString();
                    std::vector<uint8_t> req_pt3(req_str.begin(), req_str.end());
                    req_pt3.insert(req_pt3.begin(), {0x00, 0x07}); // Type 7

                    chan_frame.channel_id = input_ch; 
                    chan_frame.payload = tls_ctx->encrypt(req_pt3);
                    std::cout << "[MAIN-TX] Sending ChannelOpenRequest on Ch " << input_ch << std::endl;
                    usb_transport.write_frame(chan_frame);

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

                    touch.init(800, 480);
                }
            }
            // --- ENCRYPTED MESSAGES (ANY CHANNEL) ---
            else if (tls_finished && (frame.flags & FLAG_ENCRYPTED))
            {
                std::vector<uint8_t> plaintext = tls_ctx->decrypt(frame.payload);
                if (plaintext.empty()) continue; 

                // Control Messages (Not Video/Audio NALUs)
                if (!(frame.flags & FLAG_MEDIA)) 
                {
                    if (plaintext.size() < 2) continue;
                    uint16_t enc_msg_type = (plaintext[0] << 8) | plaintext[1];
                    std::cout << "[MAIN-RX] Decrypted Control Type on Ch " << (int)frame.channel_id << ": " << decode_msg_type(enc_msg_type) << std::endl;

                    if (enc_msg_type == 0x0006 && auth_complete) // ServiceDiscoveryResponse
                    { 
                        ServiceDiscovery sdp_resp;
                        sdp_resp.ParseFromArray(plaintext.data() + 2, plaintext.size() - 2);
                        std::cout << "[MAIN-SDP] Connected to Head Unit: " << sdp_resp.head_unit_make() << " " << sdp_resp.head_unit_model() << std::endl;
                    }
                    else if (enc_msg_type == 0x000B) // PingRequest -> PongResponse
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
                    else if (enc_msg_type == 0x8001) // MediaSetupRequest
                    {
                        std::cout << "[MAIN-TX] Received MediaSetupRequest on Ch " << (int)frame.channel_id << ". Sending MediaSetupResponse." << std::endl;
                        
                        // Using the proper Protobuf for the response
                        MediaSetupResponse setup_resp;
                        setup_resp.set_status(STATUS_SUCCESS);
                        setup_resp.set_max_unacked(1);
                        
                        std::string resp_str = setup_resp.SerializeAsString();
                        std::vector<uint8_t> resp_pt(resp_str.begin(), resp_str.end());
                        resp_pt.insert(resp_pt.begin(), {0x80, 0x04}); // Type 0x8004
                        
                        GalFrame resp_frame;
                        resp_frame.channel_id = frame.channel_id;
                        resp_frame.flags = FLAG_FIRST | FLAG_LAST | FLAG_ENCRYPTED;
                        resp_frame.payload = tls_ctx->encrypt(resp_pt);
                        usb_transport.write_frame(resp_frame);
                    }
                    else if (enc_msg_type == 0x8002) // MediaStartRequest
                    {
                        std::cout << "[MAIN-TX] Received MediaStartRequest on Ch " << (int)frame.channel_id << ". Starting Encoder!" << std::endl;
                        if (!video_thread) {
                            video_ch = frame.channel_id;
                            video_thread = std::make_unique<VideoEncoderThread>(usb_transport, *tls_ctx, frame.channel_id);
                            video_thread->start(800, 480);
                        }
                    }
                    else if (enc_msg_type == 0x8003) // MediaStopRequest
                    {
                        std::cout << "[MAIN-TX] Received MediaStopRequest on Ch " << (int)frame.channel_id << std::endl;
                        if (video_thread && frame.channel_id == video_ch) {
                            video_thread->stop();
                            video_thread.reset();
                        }
                    }
                    else if (enc_msg_type == 0x8008) // VideoFocusNotification
                    {
                        std::cout << "[MAIN-STATE] Received VideoFocusNotification on Ch " << (int)frame.channel_id << std::endl;
                        if (!video_thread) {
                            video_ch = frame.channel_id; 
                            std::cout << "[MAIN-STATE] Starting VideoEncoderThread!" << std::endl;
                            video_thread = std::make_unique<VideoEncoderThread>(usb_transport, *tls_ctx, video_ch);
                            video_thread->start(800, 480);
                        }
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