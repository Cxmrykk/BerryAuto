#include "message_handler.hpp"
#include "globals.hpp"
#include "aap_sender.hpp"
#include "video_sender.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"

#include "control.pb.h"
#include "media.pb.h"
#include "input.pb.h"

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

void handle_decrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len) {
    if (channel == 0) {
        if (type == ControlMsgType::MESSAGE_SERVICE_DISCOVERY_RESPONSE) {
            LOG_I(">>> Service Discovery Response received. Parsing Display Specs... <<<");

            ServiceDiscoveryResponse sdp_resp;
            if (sdp_resp.ParseFromArray(payload_data, payload_len)) {
                for (int i = 0; i < sdp_resp.services_size(); i++) {
                    const auto& svc = sdp_resp.services(i);
                    // Video Sink Parsing
                    if (svc.id() == 2 && svc.has_media_sink_service()) {
                        if (svc.media_sink_service().video_configs_size() > 0) {
                            const auto& video_config = svc.media_sink_service().video_configs(0);
                            int res_type = video_config.codec_resolution();
                            
                            switch (res_type) {
                                case 1: global_video_width = 800;  global_video_height = 480;  break;
                                case 2: global_video_width = 1280; global_video_height = 720;  break;
                                case 3: global_video_width = 1920; global_video_height = 1080; break;
                                case 4: global_video_width = 2560; global_video_height = 1440; break;
                                case 5: global_video_width = 3840; global_video_height = 2160; break;
                                case 6: global_video_width = 720;  global_video_height = 1280; break;
                                case 7: global_video_width = 1080; global_video_height = 1920; break;
                                case 8: global_video_width = 1440; global_video_height = 2560; break;
                                case 9: global_video_width = 2160; global_video_height = 3840; break;
                                default: global_video_width = 800; global_video_height = 480;  break;
                            }

                            if (video_config.has_margin_width()) global_video_margin_w = video_config.margin_width();
                            if (video_config.has_margin_height()) global_video_margin_h = video_config.margin_height();
                            
                            std::cout << "[INFO] Headunit negotiated resolution: " 
                                      << global_video_width << "x" << global_video_height 
                                      << " | Margins (W: " << global_video_margin_w << " H: " << global_video_margin_h << ")" << std::endl;
                        }
                    }
                    // Input / Touch Parsing
                    if (svc.id() == 3 && svc.has_input_source_service()) {
                        if (svc.input_source_service().has_touchscreen()) {
                            global_touch_width = svc.input_source_service().touchscreen().width();
                            global_touch_height = svc.input_source_service().touchscreen().height();
                        } else {
                            global_touch_width = global_video_width;
                            global_touch_height = global_video_height;
                        }
                    }
                    // Bluetooth Connection Parsing
                    if (svc.id() == 8 && svc.has_bluetooth_service()) {
                        std::cout << "[INFO] Headunit expects Bluetooth pairing to MAC: " 
                                  << svc.bluetooth_service().car_address() << std::endl;
                        std::cout << "[INFO] Notice: Bluetooth is not fatal to the USB stream. "
                                  << "Audio will route via USB automatically. Pair the Pi to the car manually if you desire." << std::endl;
                    }
                }
            } 
            
            LOG_I(">>> Negotiating Channels... <<<");
            
            ChannelOpenRequest vid_req;
            vid_req.set_priority(1);
            vid_req.set_service_id(2);
            send_encrypted(0, ControlMsgType::MESSAGE_CHANNEL_OPEN_REQUEST, vid_req);

            ChannelOpenRequest inp_req;
            inp_req.set_priority(2);
            inp_req.set_service_id(3);
            send_encrypted(0, ControlMsgType::MESSAGE_CHANNEL_OPEN_REQUEST, inp_req);
        } 
        else if (type == ControlMsgType::MESSAGE_CHANNEL_OPEN_RESPONSE) {
            if (!video_channel_ready) {
                video_channel_ready = true;
                LOG_I(">>> Video Channel (2) Opened! Sending Media Setup... <<<");
                MediaSetupRequest setup; 
                setup.set_type(MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP);
                send_encrypted(2, MEDIA_MESSAGE_SETUP, setup); 
            } 
            else if (!input_channel_ready) {
                input_channel_ready = true;
                LOG_I(">>> Input Channel (3) Opened! Sending Touch Binding Request... <<<");
                KeyBindingRequest bind;
                send_encrypted(3, InputMsgType::BINDINGREQUEST, bind);
            }
        }
        else if (type == ControlMsgType::MESSAGE_PING_REQUEST) {
            PingRequest req;
            req.ParseFromArray(payload_data, payload_len);
            PingResponse resp;
            resp.set_timestamp(req.timestamp());
            send_encrypted(0, ControlMsgType::MESSAGE_PING_RESPONSE, resp);
        }
        else if (type == ControlMsgType::MESSAGE_BYEBYE_REQUEST) {
            is_video_streaming = false;
            video_channel_ready = false;
            input_channel_ready = false;
            ByeByeResponse resp;
            send_encrypted(0, ControlMsgType::MESSAGE_BYEBYE_RESPONSE, resp);
        }
        else if (type == ControlMsgType::MESSAGE_CHANNEL_CLOSE_NOTIFICATION) {
            is_video_streaming = false;
            video_channel_ready = false;
        }
    }
    else if (channel == 2) { 
        if (type == MediaMsgType::MEDIA_MESSAGE_CONFIG) { 
            Config config;
            if (config.ParseFromArray(payload_data, payload_len)) {
                if (config.has_max_unacked()) {
                    max_video_unacked = config.max_unacked();
                    LOG_I(">>> Max Unacked Frames set to " + std::to_string(max_video_unacked));
                }
            }
            
            LOG_I(">>> Video Negotiated. Starting Stream! <<<");
            Start start; 
            start.set_session_id(1234);
            start.set_configuration_index(0);
            send_encrypted(2, MEDIA_MESSAGE_START, start);

            is_video_streaming = true;
            video_unacked_count = 0; 
            
            if (video_streamer == nullptr) {
                video_streamer = new VideoEncoder(global_video_width, global_video_height, on_video_nal_ready);
                video_streamer->start();
                std::cout << "[VIDEO] Live Encoding Started (" << global_video_width << "x" << global_video_height << " H.264)." << std::endl;
            }

            // Push initial keyframe and configs
            video_streamer->force_keyframe();
            inject_cached_video_config();
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_ACK) {
            Ack ack_msg;
            if (ack_msg.ParseFromArray(payload_data, payload_len)) {
                video_unacked_count -= ack_msg.ack();
                if (video_unacked_count.load() < 0) video_unacked_count = 0;
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_START) {
            is_video_streaming = true;
            video_unacked_count = 0;
            inject_cached_video_config();
            if (video_streamer) video_streamer->force_keyframe();
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_STOP) {
            is_video_streaming = false;
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION) {
            VideoFocusNotification focus_notif;
            if (focus_notif.ParseFromArray(payload_data, payload_len)) {
                if (focus_notif.mode() == VideoFocusMode::VIDEO_FOCUS_PROJECTED) {
                    is_video_streaming = true;
                    video_unacked_count = 0; 
                    if (video_streamer) video_streamer->force_keyframe();
                    inject_cached_video_config();
                } else {
                    is_video_streaming = false;
                }
            }
        }
    }
    else if (channel == 3 && type == InputMsgType::EVENT) {
        InputReport report;
        report.ParseFromArray(payload_data, payload_len);
        handle_touch_event(report);
    }
}

void handle_unencrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len) {
    (void)payload_data; // Silence unused warning
    (void)payload_len;

    if (channel == 0 && type == ControlMsgType::MESSAGE_VERSION_REQUEST) {
        LOG_I(">>> Version Request Received. Initiating AAP Handshake... <<<");
        
        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);

            is_tls_connected = false;
            video_channel_ready = false;
            input_channel_ready = false;
            is_video_streaming = false;
            video_unacked_count = 0;

            SSL_clear(ssl);
            SSL_set_accept_state(ssl);
            
            (void)BIO_reset(rbio);
            (void)BIO_reset(wbio);
        }

        std::vector<uint8_t> resp_payload = {0x00, 0x01, 0x00, 0x01}; 
        send_unencrypted(0, 0x03, ControlMsgType::MESSAGE_VERSION_RESPONSE, resp_payload);
    } 
    else if (channel == 0 && type == ControlMsgType::MESSAGE_ENCAPSULATED_SSL) {
        bool handshake_just_completed = false;
        
        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            BIO_write(rbio, payload_data, payload_len);
            
            if (!is_tls_connected) {
                int ret = SSL_accept(ssl);
                if (ret == 1) {
                    LOG_I(">>> TLS Handshake Complete! <<<");
                    handshake_just_completed = true;
                }
            }
        }
        
        // Flush the TLS handshake data BEFORE updating is_tls_connected to true.
        ssl_write_and_flush_unlocked({}, 0, 0x0B, 0); 
        
        if (handshake_just_completed) {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            is_tls_connected = true;
        }
    }
    else if (channel == 0 && type == ControlMsgType::MESSAGE_AUTH_COMPLETE) {
        LOG_I(">>> Authentication Passed. Sending Service Discovery Request... <<<");
        ServiceDiscoveryRequest sdp_req;
        sdp_req.set_phone_name("BerryAuto Phone");
        sdp_req.set_phone_brand("Raspberry Pi");
        send_encrypted(0, ControlMsgType::MESSAGE_SERVICE_DISCOVERY_REQUEST, sdp_req);
    }
}