#include "handler_control.hpp"
#include "aap_sender.hpp"
#include "channel_manager.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <openssl/ssl.h> // ADDED

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

void handle_control_message(uint16_t type, uint8_t* payload_data, int payload_len)
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
        video_session_id++;
        stop_video_stream();
        video_channel_ready = false;
        input_channel_ready = false;
        ByeByeResponse resp;
        send_message(0, ControlMsgType::MESSAGE_BYEBYE_RESPONSE, resp);

        std::lock_guard<std::recursive_mutex> lock(aap_mutex);
        is_tls_connected = false;
        ssl_bypassed = false;
        SSL_clear(ssl);
        BIO_reset(rbio);
        BIO_reset(wbio);
        flush_usb_tx_queue();
    }
    else if (type == ControlMsgType::MESSAGE_CHANNEL_CLOSE_NOTIFICATION)
    {
        video_session_id++;
        stop_video_stream();
        video_channel_ready = false;
    }
}
