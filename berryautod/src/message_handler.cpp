#include "message_handler.hpp"
#include "aap_sender.hpp"
#include "channel_manager.hpp"
#include "globals.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"
#include "video_sender.hpp"

#include "control.pb.h"
#include "input.pb.h"
#include "media.pb.h"
#include "sensors.pb.h"
#include <iomanip>
#include <openssl/err.h>
#include <openssl/ssl.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

void hex_dump(const std::string& prefix, const uint8_t* data, int len)
{
    std::cout << prefix << " (" << len << " bytes): ";
    for (int i = 0; i < len; ++i)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::dec << std::endl;
}

void handle_parsed_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len)
{
    // Ignore Video ACKs to prevent log flooding
    if (channel != 2 || type != MediaMsgType::MEDIA_MESSAGE_ACK)
    {
        std::cout << "[DEBUG-RX] Parsed - Channel: " << (int)channel << " Type: " << type << " Len: " << payload_len
                  << std::endl;
    }

    if (type == ControlMsgType::MESSAGE_CHANNEL_OPEN_RESPONSE)
    {
        handle_channel_open_response();
        return;
    }

    ChannelType ctype = channel_types[channel];

    if (channel == 0)
    {
        if (type == ControlMsgType::MESSAGE_SERVICE_DISCOVERY_RESPONSE)
        {
            process_service_discovery_response(payload_data, payload_len);
        }
        else if (type == ControlMsgType::MESSAGE_AUDIO_FOCUS_REQUEST)
        {
            LOG_I(">>> Head Unit requested Audio Focus. Yielding control automatically. <<<");
            AudioFocusNotification afn;
            afn.set_focus_state(AudioFocusNotification::STATE_GAIN);
            send_message(0, ControlMsgType::MESSAGE_AUDIO_FOCUS_NOTIFICATION, afn);
        }
        else if (type == ControlMsgType::MESSAGE_PING_REQUEST)
        {
            PingRequest req;
            req.ParseFromArray(payload_data, payload_len);
            PingResponse resp;
            if (req.has_timestamp())
                resp.set_timestamp(req.timestamp());
            send_message(0, ControlMsgType::MESSAGE_PING_RESPONSE, resp);
        }
        else if (type == ControlMsgType::MESSAGE_BYEBYE_REQUEST)
        {
            stop_video_stream();
            video_channel_ready = false;
            input_channel_ready = false;
            ByeByeResponse resp;
            send_message(0, ControlMsgType::MESSAGE_BYEBYE_RESPONSE, resp);
        }
        else if (type == ControlMsgType::MESSAGE_CHANNEL_CLOSE_NOTIFICATION)
        {
            stop_video_stream();
            video_channel_ready = false;
        }
    }
    else if (ctype == ChannelType::VIDEO || ctype == ChannelType::AUDIO || ctype == ChannelType::MIC)
    {
        if (type == MediaMsgType::MEDIA_MESSAGE_CONFIG)
        {
            if (ctype == ChannelType::VIDEO)
            {
                Config config;
                if (config.ParseFromArray(payload_data, payload_len))
                {
                    if (config.has_max_unacked())
                    {
                        max_video_unacked = config.max_unacked();
                        LOG_I(">>> Max Unacked Frames set to " + std::to_string(max_video_unacked));
                    }
                }
                LOG_I(">>> Video Configured via Car Response. Requesting Start and Focus... <<<");

                Start start;
                start.set_session_id(1234);
                start.set_configuration_index(global_video_config_index);
                send_message(channel, MediaMsgType::MEDIA_MESSAGE_START, start);

                VideoFocusRequestNotification vfr;
                vfr.set_disp_channel_id(channel);
                vfr.set_mode(VideoFocusMode::VIDEO_FOCUS_PROJECTED);
                send_message(channel, MediaMsgType::MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST, vfr);

                AudioFocusRequestNotification afr;
                afr.set_request(AudioFocusRequestNotification::GAIN);
                send_message(0, ControlMsgType::MESSAGE_AUDIO_FOCUS_REQUEST, afr);
            }
            else
            {
                LOG_I(">>> Audio/Mic Configured via Car Response. Sending Start... <<<");
                Start start;
                start.set_session_id(5678);
                start.set_configuration_index(0);
                send_message(channel, MediaMsgType::MEDIA_MESSAGE_START, start);
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_ACK)
        {
            if (ctype == ChannelType::VIDEO)
            {
                Ack ack_msg;
                if (ack_msg.ParseFromArray(payload_data, payload_len))
                {
                    if (ack_msg.has_ack())
                        video_unacked_count -= ack_msg.ack();
                    if (video_unacked_count.load() < 0)
                        video_unacked_count = 0;
                }
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_START)
        {
            if (ctype == ChannelType::VIDEO)
            {
                LOG_I(">>> Video Stream started by car! <<<");
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_STOP)
        {
            if (ctype == ChannelType::VIDEO)
            {
                LOG_I(">>> Video Stream stopped by car! <<<");
                stop_video_stream();
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST)
        {
            if (ctype == ChannelType::VIDEO)
            {
                LOG_I(">>> Head Unit requested Video Focus. Yielding control automatically. <<<");
                VideoFocusNotification vfn;
                vfn.set_mode(VideoFocusMode::VIDEO_FOCUS_PROJECTED);
                send_message(channel, MediaMsgType::MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION, vfn);
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION)
        {
            if (ctype == ChannelType::VIDEO)
            {
                VideoFocusNotification focus_notif;
                if (focus_notif.ParseFromArray(payload_data, payload_len))
                {
                    if (focus_notif.has_mode() && focus_notif.mode() == VideoFocusMode::VIDEO_FOCUS_PROJECTED)
                    {
                        if (!is_video_streaming.load())
                        {
                            LOG_I(">>> Car GRANTED Video Focus! Streaming active. <<<");
                            is_video_streaming = true;
                            video_unacked_count = 0;
                            if (video_streamer == nullptr)
                            {
                                video_streamer =
                                    new VideoEncoder(global_video_width, global_video_height, on_video_nal_ready);
                                video_streamer->start();
                                std::cout << "[VIDEO] Live Encoding Started (" << global_video_width << "x"
                                          << global_video_height << ")." << std::endl;
                            }
                            video_streamer->force_keyframe();
                        }
                    }
                    else
                    {
                        LOG_I(">>> Car REVOKED Video Focus. <<<");
                        stop_video_stream();
                    }
                }
            }
        }
        else if (type == MediaMsgType::MEDIA_MESSAGE_MICROPHONE_REQUEST)
        {
            if (ctype == ChannelType::MIC)
            {
                LOG_I(">>> Car Requested Microphone start/stop <<<");
                MicrophoneResponse mr;
                mr.set_status(0);
                mr.set_session_id(0);
                send_message(channel, MediaMsgType::MEDIA_MESSAGE_MICROPHONE_RESPONSE, mr);
            }
        }
    }
    else if (ctype == ChannelType::SENSOR)
    {
        if (type == SensorsMsgType::SENSOR_STARTREQUEST)
        {
            SensorRequest req;
            req.ParseFromArray(payload_data, payload_len);
            LOG_I(">>> Sensor Start Request for Sensor Type: " << req.type() << " <<<");

            SensorResponse resp;
            resp.set_status(MessageStatus::STATUS_SUCCESS);
            send_message(channel, SensorsMsgType::SENSOR_STARTRESPONSE, resp);

            if (req.type() == SensorType::DRIVING_STATUS)
            {
                SensorBatch batch;
                auto* driving = batch.add_driving_status();
                driving->set_status(SensorBatch_DrivingStatusData_Status_UNRESTRICTED);
                send_message(channel, SensorsMsgType::SENSOR_EVENT, batch);
                LOG_I(">>> Sent Driving Status (Unrestricted) <<<");
            }
            else if (req.type() == SensorType::NIGHT)
            {
                SensorBatch batch;
                auto* night = batch.add_night_mode();
                night->set_is_night_mode(false); // Default to day mode
                send_message(channel, SensorsMsgType::SENSOR_EVENT, batch);
                LOG_I(">>> Sent Night Mode Status (Day) <<<");
            }
        }
    }
    else if (ctype == ChannelType::INPUT)
    {
        if (type == InputMsgType::EVENT)
        {
            InputReport report;
            report.ParseFromArray(payload_data, payload_len);
            handle_touch_event(report);
        }
        else if (type == InputMsgType::BINDINGRESPONSE)
        {
            LOG_I(">>> Touch Binding Response Received on Channel " << (int)channel << " <<<");
        }
    }
}

void handle_decrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len)
{
    handle_parsed_payload(channel, type, payload_data, payload_len);
}

void handle_unencrypted_payload(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len)
{
    if (channel == 0 && type == ControlMsgType::MESSAGE_VERSION_REQUEST)
    {
        uint16_t major = 1;
        uint16_t minor = 6;

        if (payload_len >= 4)
        {
            major = (payload_data[0] << 8) | payload_data[1];
            minor = (payload_data[2] << 8) | payload_data[3];
        }

        std::cout << "[INFO] Head Unit requested AAP Version " << major << "." << minor << std::endl;
        LOG_I(">>> Version Request Received. Initiating AAP Handshake... <<<");

        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);

            is_tls_connected = false;
            video_channel_ready = false;
            input_channel_ready = false;
            stop_video_stream();
            flush_usb_tx_queue();
            video_unacked_count = 0;

            SSL_clear(ssl);
            SSL_set_accept_state(ssl);

            (void)BIO_reset(rbio);
            (void)BIO_reset(wbio);
        }

        std::vector<uint8_t> resp_payload = {
            (uint8_t)(major >> 8), (uint8_t)(major & 0xFF), (uint8_t)(minor >> 8), (uint8_t)(minor & 0xFF), 0x00, 0x00};
        send_unencrypted(0, 0x03, ControlMsgType::MESSAGE_VERSION_RESPONSE, resp_payload);
    }
    else if (channel == 0 && type == ControlMsgType::MESSAGE_ENCAPSULATED_SSL)
    {
        bool handshake_just_completed = false;

        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            BIO_write(rbio, payload_data, payload_len);

            if (!is_tls_connected)
            {
                int ret = SSL_accept(ssl);
                if (ret == 1)
                {
                    LOG_I(">>> TLS Handshake Complete! <<<");
                    handshake_just_completed = true;
                }
                else
                {
                    int err = SSL_get_error(ssl, ret);
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                    {
                        LOG_E(">>> SSL_accept Error: " << err << " <<<");
                    }
                }
            }
        }

        ssl_write_and_flush_unlocked({}, 0, 0x0B, 0); // Correctly flushed

        if (handshake_just_completed)
        {
            std::lock_guard<std::recursive_mutex> lock(aap_mutex);
            is_tls_connected = true;
        }
    }
    else if (channel == 0 && type == ControlMsgType::MESSAGE_AUTH_COMPLETE)
    {
        AuthCompleteResponse auth_resp;
        if (auth_resp.ParseFromArray(payload_data, payload_len))
        {
            if (auth_resp.has_status() && auth_resp.status() != 0)
            {
                LOG_I(">>> IGNORING Auth Failure! Attempting to forcefully bypass and proceed... <<<");
            }
            else
            {
                LOG_I(">>> Head Unit sent AuthComplete(0) - Authentication SUCCESS! <<<");
            }
        }

        if (!is_tls_connected)
        {
            ssl_bypassed = true;
            LOG_I(">>> Head Unit bypassed TLS! Operating in plaintext mode. <<<");
        }

        LOG_I(">>> Sending Service Discovery Request... <<<");
        ServiceDiscoveryRequest sdp_req;
        sdp_req.set_phone_name("BerryAuto Phone");
        sdp_req.set_phone_brand("Raspberry Pi");

        if (ssl_bypassed)
        {
            std::vector<uint8_t> serialized(sdp_req.ByteSizeLong());
            sdp_req.SerializeToArray(serialized.data(), serialized.size());
            send_unencrypted(0, 0x03, ControlMsgType::MESSAGE_SERVICE_DISCOVERY_REQUEST, serialized);
        }
        else
        {
            send_message(0, ControlMsgType::MESSAGE_SERVICE_DISCOVERY_REQUEST, sdp_req);
        }
    }
    else
    {
        handle_parsed_payload(channel, type, payload_data, payload_len);
    }
}
