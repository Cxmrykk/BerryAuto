#include "protocol_framer.h"
#include <cstring>
#include <iostream>

std::vector<uint8_t> GalFrame::serialize() const {
    // Unfragmented frame serialization
    std::vector<uint8_t> buffer(4 + payload.size());
    buffer[0] = channel_id; 
    buffer[1] = flags;
    buffer[2] = (payload.size() >> 8) & 0xFF; 
    buffer[3] = payload.size() & 0xFF;
    std::memcpy(buffer.data() + 4, payload.data(), payload.size());
    return buffer;
}

void Reassembler::append(const uint8_t* buffer, size_t length, std::vector<GalFrame>& frames) {
    partial_buffer.insert(partial_buffer.end(), buffer, buffer + length);
    
    while (partial_buffer.size() >= 4) {
        uint16_t payload_len = (partial_buffer[2] << 8) | partial_buffer[3];
        size_t header_size = 4;
        
        // If this is the FIRST fragment of a fragmented message (FIRST=1, LAST=0)
        // A 4-byte 'Total Payload Size' field is injected into the header.
        if ((partial_buffer[1] & FLAG_FIRST) && !(partial_buffer[1] & FLAG_LAST)) {
            header_size = 8;
        }

        if (partial_buffer.size() < header_size + payload_len) {
            break;
        }

        GalFrame frame;
        frame.channel_id = partial_buffer[0]; 
        frame.flags = partial_buffer[1];
        frame.payload.assign(partial_buffer.begin() + header_size, partial_buffer.begin() + header_size + payload_len);
        frames.push_back(frame);
        
        partial_buffer.erase(partial_buffer.begin(), partial_buffer.begin() + header_size + payload_len);
    }
}