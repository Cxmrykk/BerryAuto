#include "transport_ffs.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/usb/functionfs.h>
#include <iostream>
#include <cstring>

#define CPU_TO_LE32(x) (x)
#define CPU_TO_LE16(x) (x)

struct ffs_descriptors_t {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_endpoint_descriptor_no_audio sink;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed));

struct ffs_strings_t {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        char str1[sizeof("OpenGAL Interface")]; // Removed 'const' here
    } __attribute__((packed)) lang0;
} __attribute__((packed));

// Zero-initialize the globals
ffs_descriptors_t ffs_descriptors = {};
ffs_strings_t ffs_strings = {};

FunctionFSTransport::FunctionFSTransport(const std::string& mount_point) 
    : path(mount_point), ep0_fd(-1), ep1_in_fd(-1), ep2_out_fd(-1) {}

FunctionFSTransport::~FunctionFSTransport() {
    if (ep1_in_fd >= 0) close(ep1_in_fd);
    if (ep2_out_fd >= 0) close(ep2_out_fd);
    if (ep0_fd >= 0) close(ep0_fd);
}

bool FunctionFSTransport::init() {
    ep0_fd = open((path + "/ep0").c_str(), O_RDWR);
    if (ep0_fd < 0) { std::cerr << "Failed to open FFS ep0\n"; return false; }

    ffs_descriptors.header.magic = CPU_TO_LE32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    ffs_descriptors.header.length = CPU_TO_LE32(sizeof(ffs_descriptors));
    ffs_descriptors.header.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC;
    ffs_descriptors.fs_count = 3; ffs_descriptors.hs_count = 3;

    ffs_descriptors.fs_descs.intf.bLength = sizeof(ffs_descriptors.fs_descs.intf);
    ffs_descriptors.fs_descs.intf.bDescriptorType = USB_DT_INTERFACE;
    ffs_descriptors.fs_descs.intf.bInterfaceNumber = 0;
    ffs_descriptors.fs_descs.intf.bNumEndpoints = 2;
    ffs_descriptors.fs_descs.intf.bInterfaceClass = 0xFF; 
    ffs_descriptors.fs_descs.intf.bInterfaceSubClass = 0xFF;
    ffs_descriptors.fs_descs.intf.bInterfaceProtocol = 0x00;
    ffs_descriptors.fs_descs.intf.iInterface = 1;

    ffs_descriptors.fs_descs.source.bLength = sizeof(ffs_descriptors.fs_descs.source);
    ffs_descriptors.fs_descs.source.bDescriptorType = USB_DT_ENDPOINT;
    ffs_descriptors.fs_descs.source.bEndpointAddress = 1 | USB_DIR_IN;
    ffs_descriptors.fs_descs.source.bmAttributes = USB_ENDPOINT_XFER_BULK;
    ffs_descriptors.fs_descs.source.wMaxPacketSize = CPU_TO_LE16(512);

    ffs_descriptors.fs_descs.sink.bLength = sizeof(ffs_descriptors.fs_descs.sink);
    ffs_descriptors.fs_descs.sink.bDescriptorType = USB_DT_ENDPOINT;
    ffs_descriptors.fs_descs.sink.bEndpointAddress = 2 | USB_DIR_OUT;
    ffs_descriptors.fs_descs.sink.bmAttributes = USB_ENDPOINT_XFER_BULK;
    ffs_descriptors.fs_descs.sink.wMaxPacketSize = CPU_TO_LE16(512);

    ffs_descriptors.hs_descs = ffs_descriptors.fs_descs;
    write(ep0_fd, &ffs_descriptors, sizeof(ffs_descriptors));

    ffs_strings.header.magic = CPU_TO_LE32(FUNCTIONFS_STRINGS_MAGIC);
    ffs_strings.header.length = CPU_TO_LE32(sizeof(ffs_strings));
    ffs_strings.header.str_count = CPU_TO_LE32(1);
    ffs_strings.header.lang_count = CPU_TO_LE32(1);
    ffs_strings.lang0.code = CPU_TO_LE16(0x0409);
    strncpy(ffs_strings.lang0.str1, "OpenGAL Interface", sizeof(ffs_strings.lang0.str1));
    write(ep0_fd, &ffs_strings, sizeof(ffs_strings));

    ep1_in_fd = open((path + "/ep1").c_str(), O_WRONLY);
    ep2_out_fd = open((path + "/ep2").c_str(), O_RDONLY);
    return true;
}

void FunctionFSTransport::write_frame(const GalFrame& frame) {
    std::vector<uint8_t> data = frame.serialize();
    std::lock_guard<std::mutex> lock(write_mutex);
    write(ep1_in_fd, data.data(), data.size());
}

std::vector<GalFrame> FunctionFSTransport::read_frames() {
    uint8_t buffer[16384];
    ssize_t bytes = read(ep2_out_fd, buffer, sizeof(buffer));
    std::vector<GalFrame> frames;
    if (bytes > 0) reassembler.append(buffer, bytes, frames);
    return frames;
}