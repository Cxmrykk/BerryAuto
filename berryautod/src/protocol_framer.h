#pragma once
#include <cstdint>
#include <vector>

#define FLAG_FIRST     0x01
#define FLAG_LAST      0x02
#define FLAG_MEDIA     0x04
#define FLAG_ENCRYPTED 0x08

struct GalFrame {
    uint8_t channel_id;
    uint8_t flags;
    uint32_t timestamp;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> serialize() const;
};

class Reassembler {
public:
    void append(const uint8_t* buffer, size_t length, std::vector<GalFrame>& completed_frames);
private:
    std::vector<uint8_t> partial_buffer;
};
