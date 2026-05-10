#include "ffs_usb.hpp"
#include <endian.h>
#include <fcntl.h>
#include <iostream>
#include <linux/usb/functionfs.h>
#include <stdio.h>
#include <unistd.h>

#ifndef FUNCTIONFS_ALL_CTRL_RECIP
#define FUNCTIONFS_ALL_CTRL_RECIP 64
#endif

struct
{
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    struct
    {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio ep_in;
        struct usb_endpoint_descriptor_no_audio ep_out;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) descriptors = {.header =
                                             {
                                                 .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
                                                 .length = htole32(sizeof(descriptors)),
                                                 .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
                                                                  FUNCTIONFS_ALL_CTRL_RECIP),
                                             },
                                         .fs_count = htole32(3),
                                         .hs_count = htole32(3),
                                         .fs_descs =
                                             {
                                                 .intf = {.bLength = sizeof(descriptors.fs_descs.intf),
                                                          .bDescriptorType = USB_DT_INTERFACE,
                                                          .bInterfaceNumber = 0,
                                                          .bAlternateSetting = 0,
                                                          .bNumEndpoints = 2,
                                                          .bInterfaceClass = 255,
                                                          .bInterfaceSubClass = 255,
                                                          .bInterfaceProtocol = 0,
                                                          .iInterface = 1},
                                                 .ep_in = {.bLength = sizeof(descriptors.fs_descs.ep_in),
                                                           .bDescriptorType = USB_DT_ENDPOINT,
                                                           .bEndpointAddress = 1 | USB_DIR_IN,
                                                           .bmAttributes = USB_ENDPOINT_XFER_BULK,
                                                           .wMaxPacketSize = htole16(64),
                                                           .bInterval = 0},
                                                 .ep_out = {.bLength = sizeof(descriptors.fs_descs.ep_out),
                                                            .bDescriptorType = USB_DT_ENDPOINT,
                                                            .bEndpointAddress = 2 | USB_DIR_OUT,
                                                            .bmAttributes = USB_ENDPOINT_XFER_BULK,
                                                            .wMaxPacketSize = htole16(64),
                                                            .bInterval = 0},
                                             },
                                         .hs_descs = {
                                             .intf = {.bLength = sizeof(descriptors.hs_descs.intf),
                                                      .bDescriptorType = USB_DT_INTERFACE,
                                                      .bInterfaceNumber = 0,
                                                      .bAlternateSetting = 0,
                                                      .bNumEndpoints = 2,
                                                      .bInterfaceClass = 255,
                                                      .bInterfaceSubClass = 255,
                                                      .bInterfaceProtocol = 0,
                                                      .iInterface = 1},
                                             .ep_in = {.bLength = sizeof(descriptors.hs_descs.ep_in),
                                                       .bDescriptorType = USB_DT_ENDPOINT,
                                                       .bEndpointAddress = 1 | USB_DIR_IN,
                                                       .bmAttributes = USB_ENDPOINT_XFER_BULK,
                                                       .wMaxPacketSize = htole16(512),
                                                       .bInterval = 0},
                                             .ep_out = {.bLength = sizeof(descriptors.hs_descs.ep_out),
                                                        .bDescriptorType = USB_DT_ENDPOINT,
                                                        .bEndpointAddress = 2 | USB_DIR_OUT,
                                                        .bmAttributes = USB_ENDPOINT_XFER_BULK,
                                                        .wMaxPacketSize = htole16(512),
                                                        .bInterval = 0},
                                         }};

struct
{
    struct usb_functionfs_strings_head header;
    struct
    {
        __le16 code;
        const char str1[sizeof("OpenGAL Interface")];
    } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {.header = {.magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
                                                .length = htole32(sizeof(strings)),
                                                .str_count = htole32(1),
                                                .lang_count = htole32(1)},
                                     .lang0 = {.code = htole16(0x0409), .str1 = "OpenGAL Interface"}};

bool init_ffs(int& ep0, int& ep_in, int& ep_out)
{
    ep0 = open("/dev/ffs-opengal/ep0", O_RDWR);
    if (ep0 < 0)
    {
        perror("[FFS] open ep0 failed");
        return false;
    }

    if (write(ep0, &descriptors, sizeof(descriptors)) < 0)
    {
        perror("[FFS] write descriptors failed");
        return false;
    }

    if (write(ep0, &strings, sizeof(strings)) < 0)
    {
        perror("[FFS] write strings failed");
        return false;
    }

    ep_in = open("/dev/ffs-opengal/ep1", O_RDWR);
    if (ep_in < 0)
    {
        perror("[FFS] open ep1 failed");
        return false;
    }

    ep_out = open("/dev/ffs-opengal/ep2", O_RDWR);
    if (ep_out < 0)
    {
        perror("[FFS] open ep2 failed");
        return false;
    }

    return true;
}
