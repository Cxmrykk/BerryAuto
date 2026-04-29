#pragma once
#include <stdint.h>
#include <vector>

struct AapMessage
{
    uint8_t channel;
    uint8_t flags;
    uint16_t msg_type;
    std::vector<uint8_t> payload;

    std::vector<uint8_t> serialize() const
    {
        std::vector<uint8_t> buf(6 + payload.size());
        buf[0] = channel;
        buf[1] = flags;
        uint16_t total_len = payload.size() + 2;
        buf[2] = (total_len >> 8) & 0xFF;
        buf[3] = total_len & 0xFF;
        buf[4] = (msg_type >> 8) & 0xFF;
        buf[5] = msg_type & 0xFF;
        std::copy(payload.begin(), payload.end(), buf.begin() + 6);
        return buf;
    }
};
