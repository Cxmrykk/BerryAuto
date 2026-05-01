#include "aap_sender.hpp"
#include "control.pb.h"
#include "globals.hpp"
#include <algorithm>
#include <condition_variable>
#include <errno.h>
#include <mutex>
#include <queue>
#include <string.h>
#include <thread>
#include <unistd.h>

using namespace com::andrerinas::headunitrevived::aap::protocol::proto;

// STRICT SINGLE-FIFO QUEUE: Guarantees TLS Sequence numbers exactly match USB transmission order!
std::queue<std::vector<uint8_t>> tx_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::once_flag tx_thread_flag;

void tx_worker()
{
    while (true)
    {
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !tx_queue.empty(); });
            chunk = tx_queue.front();
            tx_queue.pop();
        }

        const uint8_t* ptr = chunk.data();
        size_t remain = chunk.size();
        while (remain > 0)
        {
            int w = write(ep_in, ptr, remain);
            if (w < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                {
                    usleep(100);
                    continue;
                }
                LOG_E("USB TX Write Failed! " << strerror(errno));
                break;
            }
            ptr += w;
            remain -= w;
        }
    }
}

void init_tx_thread()
{
    std::call_once(tx_thread_flag, [] { std::thread(tx_worker).detach(); });
}

void flush_usb_tx_queue()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::queue<std::vector<uint8_t>> empty;
    std::swap(tx_queue, empty);
}

int get_tx_queue_size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tx_queue.size();
}

void queue_packet(uint8_t channel, bool is_encrypted, bool is_control, const std::vector<uint8_t>& plaintext)
{
    init_tx_thread();
    std::vector<uint8_t> out;

    // SIMULTANEOUS LOCK: Ties the generation of the TLS Sequence number directly to its position in the TX Queue!
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    std::lock_guard<std::mutex> tx_lock(queue_mutex);

    if (is_encrypted && !ssl_bypassed)
    {
        SSL_write(ssl, plaintext.data(), plaintext.size());
        int pending = BIO_ctrl_pending(wbio);
        if (pending > 0)
        {
            std::vector<uint8_t> ciphertext(pending);
            BIO_read(wbio, ciphertext.data(), pending);

            uint8_t flag = (is_control && channel != 0) ? 0x0F : 0x0B;
            uint16_t len = ciphertext.size();

            out.push_back(channel);
            out.push_back(flag);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);
            out.insert(out.end(), ciphertext.begin(), ciphertext.end());
        }
    }
    else
    {
        uint8_t flag = (is_control && channel != 0) ? 0x07 : 0x03;
        uint16_t len = plaintext.size();

        out.push_back(channel);
        out.push_back(flag);
        out.push_back(len >> 8);
        out.push_back(len & 0xFF);
        out.insert(out.end(), plaintext.begin(), plaintext.end());
    }

    if (!out.empty())
    {
        tx_queue.push(std::move(out));
        queue_cv.notify_one();
    }
}

void flush_ssl_buffers()
{
    init_tx_thread();
    std::lock_guard<std::recursive_mutex> aap_lock(aap_mutex);
    std::lock_guard<std::mutex> tx_lock(queue_mutex);

    int pending = BIO_ctrl_pending(wbio);
    if (pending > 0)
    {
        std::vector<uint8_t> tls_record(pending);
        BIO_read(wbio, tls_record.data(), pending);

        std::vector<uint8_t> out;
        uint16_t len = tls_record.size();

        if (!is_tls_connected)
        {
            out.push_back(0);
            out.push_back(0x03);
            out.push_back((len + 2) >> 8);
            out.push_back((len + 2) & 0xFF);
            out.push_back((ControlMsgType::MESSAGE_ENCAPSULATED_SSL >> 8) & 0xFF);
            out.push_back(ControlMsgType::MESSAGE_ENCAPSULATED_SSL & 0xFF);
        }
        else
        {
            out.push_back(0);
            out.push_back(0x0B);
            out.push_back(len >> 8);
            out.push_back(len & 0xFF);
        }
        out.insert(out.end(), tls_record.begin(), tls_record.end());

        tx_queue.push(std::move(out));
        queue_cv.notify_one();
    }
}

void send_unencrypted(uint8_t channel, uint16_t type, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> pt;
    pt.push_back(type >> 8);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), payload.begin(), payload.end());

    queue_packet(channel, false, true, pt);
}

void send_message(uint8_t channel, uint16_t type, const google::protobuf::Message& proto_msg)
{
    std::vector<uint8_t> serialized(proto_msg.ByteSizeLong());
    proto_msg.SerializeToArray(serialized.data(), serialized.size());

    if (ssl_bypassed)
    {
        send_unencrypted(channel, type, serialized);
        return;
    }

    std::vector<uint8_t> pt;
    pt.push_back(type >> 8);
    pt.push_back(type & 0xFF);
    pt.insert(pt.end(), serialized.begin(), serialized.end());

    bool is_control = (type <= 26);
    queue_packet(channel, true, is_control, pt);
}

void send_media_payload(uint8_t channel, const std::vector<uint8_t>& pt)
{
    queue_packet(channel, true, false, pt);
}
