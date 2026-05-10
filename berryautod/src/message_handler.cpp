#include "message_handler.hpp"
#include "aap_sender.hpp"
#include "channel_manager.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include "handler_control.hpp"
#include "handler_media.hpp"
#include "input.pb.h"
#include "input_handler.hpp"
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
        handle_control_message(type, payload_data, payload_len);
    }
    else if (ctype == ChannelType::VIDEO || ctype == ChannelType::AUDIO || ctype == ChannelType::MIC)
    {
        handle_media_message(channel, type, payload_data, payload_len, ctype);
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
                night->set_is_night_mode(false);
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

            // Invalidate pending sessions and kill stream
            video_session_id++;
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

        send_unencrypted(0, ControlMsgType::MESSAGE_VERSION_RESPONSE, resp_payload);
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

        flush_ssl_buffers();

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
            send_unencrypted(0, ControlMsgType::MESSAGE_SERVICE_DISCOVERY_REQUEST, serialized);
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
