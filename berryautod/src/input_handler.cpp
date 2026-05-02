#include "input_handler.hpp"
#include "globals.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/uinput.h>
#include <unistd.h>

static int uinput_fd = -1;
const int ABS_MAX_VAL = 65535;

void init_uinput()
{
    if (uinput_fd >= 0)
        return;

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0)
    {
        LOG_E("Failed to open /dev/uinput. Are you running as root?");
        return;
    }

    // Configure as an Absolute Pointer (Universally accepted by libinput as a mouse/tablet)
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);  // Acts as a universal Left-Click
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH); // Flags it as a touch-capable device

    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);

    // Tell libinput this is a direct screen-mapping device
    ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "BerryAuto Virtual Touch");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    // Abstract Kernel scale to generic 0-65535 resolution
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = ABS_MAX_VAL;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = ABS_MAX_VAL;

    write(uinput_fd, &uidev, sizeof(uidev));
    ioctl(uinput_fd, UI_DEV_CREATE);
}

void emit_uinput(int type, int code, int val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(uinput_fd, &ie, sizeof(ie));
}

void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report)
{
    std::lock_guard<std::recursive_mutex> lock(aap_mutex);
    init_uinput();

    if (uinput_fd < 0 || !report.has_touch_event() || report.touch_event().pointer_data_size() == 0)
        return;

    int action = report.touch_event().action();
    int raw_x = report.touch_event().pointer_data(0).x();
    int raw_y = report.touch_event().pointer_data(0).y();

    // Android Auto gives us coordinates in the negotiated Input space. Map it linearly to uinput.
    float ratio_x = (float)raw_x / global_touch_width;
    float ratio_y = (float)raw_y / global_touch_height;

    int mapped_x = (int)(ratio_x * ABS_MAX_VAL);
    int mapped_y = (int)(ratio_y * ABS_MAX_VAL);

    if (action == 0 || action == 5) // Down
    {
        // STEP 1: Warp the cursor exactly to the finger location FIRST
        emit_uinput(EV_ABS, ABS_X, mapped_x);
        emit_uinput(EV_ABS, ABS_Y, mapped_y);
        emit_uinput(EV_SYN, SYN_REPORT, 0);

        // STEP 2: Issue the physical click at the newly updated location
        emit_uinput(EV_KEY, BTN_LEFT, 1);
        emit_uinput(EV_KEY, BTN_TOUCH, 1);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
    }
    else if (action == 1 || action == 6) // Up
    {
        // Release the click
        emit_uinput(EV_KEY, BTN_LEFT, 0);
        emit_uinput(EV_KEY, BTN_TOUCH, 0);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
    }
    else if (action == 2) // Move (Drag)
    {
        // Update the cursor location while the button is held
        emit_uinput(EV_ABS, ABS_X, mapped_x);
        emit_uinput(EV_ABS, ABS_Y, mapped_y);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
    }
}

void cleanup_input()
{
    if (uinput_fd >= 0)
    {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        uinput_fd = -1;
    }
}
