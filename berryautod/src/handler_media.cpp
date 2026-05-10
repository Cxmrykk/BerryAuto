#include "handler_media.hpp"
#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include "media.pb.h"
#include "video_encoder.hpp"
#include "video_sender.hpp"

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

void handle_media_message(uint8_t channel, uint16_t type, uint8_t* payload_data, int payload_len, ChannelType ctype)
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
                    if (config.max_unacked() <= 2)
                    {
                        max_video_unacked = 16;
                        LOG_I(">>> Max Unacked Frames parsed as " + std::to_string(config.max_unacked()) +
                              " (Likely swapped tag). Forcing to 16.");
                    }
                    else
                    {
                        max_video_unacked = config.max_unacked();
                        LOG_I(">>> Max Unacked Frames set to " + std::to_string(max_video_unacked));
                    }
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
                uint32_t ack_value = ack_msg.has_ack() ? ack_msg.ack() : 0;
                if (ack_value == 0 && ack_msg.has_session_id())
                {
                    ack_value = 1;
                }

                if (ack_value > 0)
                {
                    video_unacked_count -= ack_value;
                    if (video_unacked_count.load() < 0)
                        video_unacked_count = 0;
                }
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
                            video_streamer = new VideoEncoder(global_video_width, global_video_height, global_video_fps,
                                                              on_video_nal_ready);
                            video_streamer->start();
                            std::cout << "[VIDEO] Live Encoding Started (" << global_video_width << "x"
                                      << global_video_height << " @ " << global_video_fps << " FPS)." << std::endl;
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
