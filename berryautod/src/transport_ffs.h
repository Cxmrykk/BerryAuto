#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include "protocol_framer.h"

class FunctionFSTransport {
public:
    FunctionFSTransport(const std::string& mount_point);
    ~FunctionFSTransport();
    bool init();
    void write_frame(const GalFrame& frame);
    void write_frame_raw(const std::vector<uint8_t>& data);
    std::vector<GalFrame> read_frames();
    bool is_running() const { return running; }
private:
    std::string path;
    int ep0_fd, ep1_in_fd, ep2_out_fd;
    std::mutex write_mutex;
    Reassembler reassembler;
    
    std::thread ep0_thread;
    std::atomic<bool> running;
    void ep0_loop();
};