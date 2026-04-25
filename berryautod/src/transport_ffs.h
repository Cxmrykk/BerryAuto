#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "protocol_framer.h"

class FunctionFSTransport {
public:
    FunctionFSTransport(const std::string& mount_point);
    ~FunctionFSTransport();
    bool init();
    void write_frame(const GalFrame& frame);
    std::vector<GalFrame> read_frames();
private:
    std::string path;
    int ep0_fd, ep1_in_fd, ep2_out_fd;
    std::mutex write_mutex;
    Reassembler reassembler;
};
