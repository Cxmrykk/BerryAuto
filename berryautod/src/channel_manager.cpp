#include "channel_manager.hpp"
#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include "input.pb.h"
#include "media.pb.h"
#include "sensors.pb.h"
#include <iostream>
#include <map>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

static std::map<int, int> channel_codecs;

void handle_channel_open_response()
{
    if (!pending_channel_opens.empty())
    {
        int opened_channel = pending_channel_opens.front();
        pending_channel_opens.pop();

        std::cout << ">>> Channel (" << opened_channel << ") Opened! <<<" << std::endl;

        ChannelType ctype = channel_types[opened_channel];

        if (ctype == ChannelType::VIDEO || ctype == ChannelType::AUDIO || ctype == ChannelType::MIC)
        {
            if (ctype == ChannelType::VIDEO)
            {
                video_channel_ready = true;
            }
            std::cout << ">>> Sending Media Setup for Channel " << opened_channel << "... <<<" << std::endl;
            MediaSetupRequest setup;
            setup.set_type((MediaCodecType)channel_codecs[opened_channel]);
            send_message(opened_channel, MediaMsgType::MEDIA_MESSAGE_SETUP, setup);
        }
        else if (ctype == ChannelType::INPUT)
        {
            input_channel_ready = true;
            std::cout << ">>> Sending Touch Binding Request... <<<" << std::endl;
            KeyBindingRequest bind;
            send_message(opened_channel, InputMsgType::BINDINGREQUEST, bind);
        }
        else if (ctype == ChannelType::SENSOR)
        {
            std::cout << ">>> Sending Sensor Start Requests... <<<" << std::endl;

            SensorRequest req_driving;
            req_driving.set_type(SensorType::DRIVING_STATUS);
            send_message(opened_channel, SensorsMsgType::SENSOR_STARTREQUEST, req_driving);

            SensorRequest req_night;
            req_night.set_type(SensorType::NIGHT);
            send_message(opened_channel, SensorsMsgType::SENSOR_STARTREQUEST, req_night);
        }
        else
        {
            std::cout << ">>> Service active. No Media Setup required. <<<" << std::endl;
        }

        if (!pending_channel_opens.empty())
        {
            int next_chan = pending_channel_opens.front();
            ChannelOpenRequest req;
            req.set_priority(1);
            req.set_service_id(next_chan);
            send_message(next_chan, ControlMsgType::MESSAGE_CHANNEL_OPEN_REQUEST, req);
        }
        else
        {
            LOG_I(">>> All channels opened successfully! Waiting for Configs... <<<");
        }
    }
}

void process_service_discovery_response(uint8_t* payload_data, int payload_len)
{
    LOG_I(">>> Service Discovery Response received. Parsing Services dynamically... <<<");

    ServiceDiscoveryResponse sdp_resp;
    if (sdp_resp.ParseFromArray(payload_data, payload_len))
    {
        while (!pending_channel_opens.empty())
            pending_channel_opens.pop();
        channel_types.clear();
        channel_codecs.clear();

        for (int i = 0; i < sdp_resp.services_size(); i++)
        {
            const auto& svc = sdp_resp.services(i);
            int svc_id = svc.has_id() ? svc.id() : -1;

            if (svc_id != -1)
            {
                if (svc.has_media_sink_service())
                {
                    if (svc.media_sink_service().video_configs_size() > 0)
                    {
                        channel_types[svc_id] = ChannelType::VIDEO;
                        video_channel_id = svc_id;

                        std::cout << "[INFO] Headunit offered " << svc.media_sink_service().video_configs_size()
                                  << " video configurations:" << std::endl;

                        int best_idx = 0;
                        int best_score = -1;

                        // Dynamically score configs based on exact OS resolution matching and Pi Hardware (H.264)
                        for (int k = 0; k < svc.media_sink_service().video_configs_size(); k++)
                        {
                            const auto& vc = svc.media_sink_service().video_configs(k);
                            int codec = vc.has_video_codec_type() ? vc.video_codec_type()
                                                                  : MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP;
                            int res_type = vc.has_codec_resolution() ? vc.codec_resolution() : 1;

                            int cfg_w = 800, cfg_h = 480;
                            std::string res_str = "Unknown";
                            switch (res_type)
                            {
                                case 1:
                                    cfg_w = 800;
                                    cfg_h = 480;
                                    res_str = "800x480";
                                    break;
                                case 2:
                                    cfg_w = 1280;
                                    cfg_h = 720;
                                    res_str = "1280x720";
                                    break;
                                case 3:
                                    cfg_w = 1920;
                                    cfg_h = 1080;
                                    res_str = "1920x1080";
                                    break;
                                case 4:
                                    cfg_w = 2560;
                                    cfg_h = 1440;
                                    res_str = "2560x1440";
                                    break;
                                case 5:
                                    cfg_w = 3840;
                                    cfg_h = 2160;
                                    res_str = "3840x2160";
                                    break;
                                case 6:
                                    cfg_w = 720;
                                    cfg_h = 1280;
                                    res_str = "720x1280";
                                    break;
                                case 7:
                                    cfg_w = 1080;
                                    cfg_h = 1920;
                                    res_str = "1080x1920";
                                    break;
                                case 8:
                                    cfg_w = 1440;
                                    cfg_h = 2560;
                                    res_str = "1440x2560";
                                    break;
                                case 9:
                                    cfg_w = 2160;
                                    cfg_h = 3840;
                                    res_str = "2160x3840";
                                    break;
                            }

                            std::cout << "  - Index " << k << ": Resolution=" << res_str << ", Codec=" << codec
                                      << std::endl;

                            int score = 0;

                            // Massively prefer H.264 natively since Pi lacks HEVC encode hardware
                            if (codec == MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP)
                                score += 10000;
                            else if (codec == MediaCodecType::MEDIA_CODEC_VIDEO_H265)
                                score += 1000;

                            // Strongly prefer an EXACT resolution match to prevent distortion and black bars
                            if (cfg_w == os_desktop_width && cfg_h == os_desktop_height)
                                score += 5000;
                            else if (res_type == 2)
                                score += 500; // 720p 2nd Choice
                            else if (res_type == 3)
                                score += 300; // 1080p 3rd Choice
                            else if (res_type == 1)
                                score += 100; // 480p 4th Choice

                            if (score > best_score)
                            {
                                best_score = score;
                                best_idx = k;
                            }
                        }

                        global_video_config_index = best_idx;
                        const auto& video_config = svc.media_sink_service().video_configs(best_idx);

                        int res_type = video_config.has_codec_resolution() ? video_config.codec_resolution() : 1;
                        switch (res_type)
                        {
                            case 1:
                                global_video_width = 800;
                                global_video_height = 480;
                                break;
                            case 2:
                                global_video_width = 1280;
                                global_video_height = 720;
                                break;
                            case 3:
                                global_video_width = 1920;
                                global_video_height = 1080;
                                break;
                            case 4:
                                global_video_width = 2560;
                                global_video_height = 1440;
                                break;
                            case 5:
                                global_video_width = 3840;
                                global_video_height = 2160;
                                break;
                            case 6:
                                global_video_width = 720;
                                global_video_height = 1280;
                                break;
                            case 7:
                                global_video_width = 1080;
                                global_video_height = 1920;
                                break;
                            case 8:
                                global_video_width = 1440;
                                global_video_height = 2560;
                                break;
                            case 9:
                                global_video_width = 2160;
                                global_video_height = 3840;
                                break;
                            default:
                                global_video_width = 800;
                                global_video_height = 480;
                                break;
                        }

                        if (video_config.has_margin_width())
                            global_video_margin_w = video_config.margin_width();
                        if (video_config.has_margin_height())
                            global_video_margin_h = video_config.margin_height();

                        int codec = video_config.has_video_codec_type() ? video_config.video_codec_type()
                                                                        : MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP;
                        channel_codecs[svc_id] = codec;
                        global_video_codec_type = codec;

                        std::cout << "[INFO] Headunit negotiated VIDEO (Channel " << svc_id << ") at "
                                  << global_video_width << "x" << global_video_height << " (Codec " << codec
                                  << ", Index " << best_idx << ")" << std::endl;
                    }
                    else
                    {
                        channel_types[svc_id] = ChannelType::AUDIO;
                        int codec = svc.media_sink_service().has_available_type()
                                        ? svc.media_sink_service().available_type()
                                        : MediaCodecType::MEDIA_CODEC_AUDIO_PCM;
                        channel_codecs[svc_id] = codec;
                        std::cout << "[INFO] Headunit advertised AUDIO (Channel " << svc_id << ", Codec " << codec
                                  << ")" << std::endl;
                    }
                }
                else if (svc.has_input_source_service())
                {
                    channel_types[svc_id] = ChannelType::INPUT;
                    input_channel_id = svc_id;
                    if (svc.input_source_service().has_touchscreen())
                    {
                        global_touch_width = svc.input_source_service().touchscreen().width();
                        global_touch_height = svc.input_source_service().touchscreen().height();
                    }
                    else
                    {
                        global_touch_width = global_video_width;
                        global_touch_height = global_video_height;
                    }
                    std::cout << "[INFO] Headunit negotiated INPUT (Channel " << svc_id << ")" << std::endl;
                }
                else if (svc.has_media_source_service())
                {
                    channel_types[svc_id] = ChannelType::MIC;
                    int codec = svc.media_source_service().has_type() ? svc.media_source_service().type()
                                                                      : MediaCodecType::MEDIA_CODEC_AUDIO_PCM;
                    channel_codecs[svc_id] = codec;
                    std::cout << "[INFO] Headunit advertised MIC (Channel " << svc_id << ", Codec " << codec << ")"
                              << std::endl;
                }
                else if (svc.has_sensor_source_service())
                {
                    channel_types[svc_id] = ChannelType::SENSOR;
                    std::cout << "[INFO] Headunit advertised SENSORS (Channel " << svc_id << ")" << std::endl;
                }
                else if (svc.has_navigation_status_service())
                {
                    channel_types[svc_id] = ChannelType::NAVIGATION;
                    std::cout << "[INFO] Headunit advertised NAVIGATION (Channel " << svc_id << ")" << std::endl;
                }
                else if (svc.has_bluetooth_service())
                {
                    channel_types[svc_id] = ChannelType::BLUETOOTH;
                    std::cout << "[INFO] Headunit advertised BLUETOOTH (Channel " << svc_id << ")" << std::endl;
                }
                else
                {
                    channel_types[svc_id] = ChannelType::UNKNOWN;
                    std::cout << "[INFO] Headunit advertised UNKNOWN SERVICE (Channel " << svc_id << ")" << std::endl;
                }

                pending_channel_opens.push(svc_id);
            }
        }
    }
    else
    {
        LOG_E(">>> [CRITICAL] ParseFromArray FAILED for ServiceDiscoveryResponse! <<<");
    }

    LOG_I(">>> Negotiating Channels Sequentially... <<<");

    if (!pending_channel_opens.empty())
    {
        int first_chan = pending_channel_opens.front();
        ChannelOpenRequest req;
        req.set_priority(1);
        req.set_service_id(first_chan);
        send_message(first_chan, ControlMsgType::MESSAGE_CHANNEL_OPEN_REQUEST, req);
    }
}
