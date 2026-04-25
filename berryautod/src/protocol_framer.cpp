#include "protocol_framer.h"
#include <cstring>

std::vector<uint8_t> GalFrame::serialize() const {
    size_t header_size = (flags & FLAG_MEDIA) ? 8 : 4;
    std::vector<uint8_t> buffer(header_size + payload.size());
    buffer[0] = channel_id; buffer[1] = flags;
    buffer[2] = (payload.size() >> 8) & 0xFF; buffer[3] = payload.size() & 0xFF;

    if (flags & FLAG_MEDIA) {
        buffer[4] = (timestamp >> 24) & 0xFF; buffer[5] = (timestamp >> 16) & 0xFF;
        buffer[6] = (timestamp >> 8) & 0xFF; buffer[7] = timestamp & 0xFF;
    }
    std::memcpy(buffer.data() + header_size, payload.data(), payload.size());
    return buffer;
}

void Reassembler::append(const uint8_t* buffer, size_t length, std::vector<GalFrame>& frames) {
    partial_buffer.insert(partial_buffer.end(), buffer, buffer + length);
    while (partial_buffer.size() >= 4) {
        uint16_t payload_len = (partial_buffer[2] << 8) | partial_buffer[3];
        size_t header_size = (partial_buffer[1] & FLAG_MEDIA) ? 8 : 4;
        if (partial_buffer.size() < header_size + payload_len) break;

        GalFrame frame;
        frame.channel_id = partial_buffer[0]; frame.flags = partial_buffer[1];
        frame.timestamp = (frame.flags & FLAG_MEDIA) ? 
            ((partial_buffer[4]<<24)|(partial_buffer[5]<<16)|(partial_buffer[6]<<8)|partial_buffer[7]) : 0;
        frame.payload.assign(partial_buffer.begin() + header_size, partial_buffer.begin() + header_size + payload_len);
        frames.push_back(frame);
        partial_buffer.erase(partial_buffer.begin(), partial_buffer.begin() + header_size + payload_len);
    }
}
