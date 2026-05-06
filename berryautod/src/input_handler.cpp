#include "input_handler.hpp"
#include "globals.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/uinput.h>
#include <unistd.h>

static int uinput_fd = -1;
const int ABS_MAX_VAL = 65535;
const int MAX_FINGERS = 10;
static int tracking_id_counter = 1;

void init_uinput()
{
    if (uinput_fd >= 0)
        return;

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0)
        return;

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_WAKEUP);

    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);

    ioctl(uinput_fd, UI_SET_EVBIT, EV_REL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_X);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_Y);

    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "BerryAuto Touchscreen");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = ABS_MAX_VAL;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = ABS_MAX_VAL;
    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = ABS_MAX_VAL;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = ABS_MAX_VAL;
    uidev.absmin[ABS_MT_SLOT] = 0;
    uidev.absmax[ABS_MT_SLOT] = MAX_FINGERS - 1;
    uidev.absmin[ABS_MT_TRACKING_ID] = 0;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;

    write(uinput_fd, &uidev, sizeof(uidev));
    ioctl(uinput_fd, UI_DEV_CREATE);
}

void emit_uinput(int type, int code, int val)
{
    if (uinput_fd < 0)
        return;
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(uinput_fd, &ie, sizeof(ie));
}

void reset_input_state()
{
    if (uinput_fd >= 0)
    {
        for (int i = 0; i < MAX_FINGERS; i++)
        {
            emit_uinput(EV_ABS, ABS_MT_SLOT, i);
            emit_uinput(EV_ABS, ABS_MT_TRACKING_ID, -1);
        }
        emit_uinput(EV_KEY, BTN_TOUCH, 0);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
    }
}

// CRITICAL FIX: Simulate a screen tap/movement to guarantee Wayland recalculates the screen and pushes a frame
void wake_up_display()
{
    if (uinput_fd < 0)
        init_uinput();
    if (uinput_fd >= 0)
    {
        emit_uinput(EV_KEY, KEY_WAKEUP, 1);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
        emit_uinput(EV_KEY, KEY_WAKEUP, 0);
        emit_uinput(EV_SYN, SYN_REPORT, 0);

        static int wiggle = 1;
        wiggle = -wiggle;

        // Simulate a physical mouse moving back and forth
        emit_uinput(EV_REL, REL_X, wiggle);
        emit_uinput(EV_REL, REL_Y, wiggle);
        emit_uinput(EV_SYN, SYN_REPORT, 0);
    }
}

void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report)
{
    std::lock_guard<std::recursive_mutex> lock(aap_mutex);
    init_uinput();

    if (uinput_fd < 0 || !report.has_touch_event() || report.touch_event().pointer_data_size() == 0)
        return;

    int action = report.touch_event().action();
    int action_index = report.touch_event().has_action_index() ? report.touch_event().action_index() : 0;
    bool is_down = (action == 0 || action == 5);
    bool is_up = (action == 1 || action == 3 || action == 6);

    for (int i = 0; i < report.touch_event().pointer_data_size(); i++)
    {
        const auto& p = report.touch_event().pointer_data(i);
        int id = p.has_pointer_id() ? p.pointer_id() : i;
        if (id < 0 || id >= MAX_FINGERS)
            continue;

        int mapped_x = (int)(((float)p.x() / global_touch_width) * ABS_MAX_VAL);
        int mapped_y = (int)(((float)p.y() / global_touch_height) * ABS_MAX_VAL);

        emit_uinput(EV_ABS, ABS_MT_SLOT, id);

        if (is_up && (action == 1 || action == 3 || (action == 6 && i == action_index)))
        {
            emit_uinput(EV_ABS, ABS_MT_TRACKING_ID, -1);
        }
        else if (is_down && (action == 0 || (action == 5 && i == action_index)))
        {
            emit_uinput(EV_ABS, ABS_MT_TRACKING_ID, tracking_id_counter++);
            if (tracking_id_counter > 65000)
                tracking_id_counter = 1;
            emit_uinput(EV_ABS, ABS_MT_POSITION_X, mapped_x);
            emit_uinput(EV_ABS, ABS_MT_POSITION_Y, mapped_y);
            if (id == 0)
            {
                emit_uinput(EV_ABS, ABS_X, mapped_x);
                emit_uinput(EV_ABS, ABS_Y, mapped_y);
            }
        }
        else if (action == 2)
        {
            emit_uinput(EV_ABS, ABS_MT_POSITION_X, mapped_x);
            emit_uinput(EV_ABS, ABS_MT_POSITION_Y, mapped_y);
            if (id == 0)
            {
                emit_uinput(EV_ABS, ABS_X, mapped_x);
                emit_uinput(EV_ABS, ABS_Y, mapped_y);
            }
        }
    }

    if (action == 0)
        emit_uinput(EV_KEY, BTN_TOUCH, 1);
    else if (action == 1 || action == 3)
        emit_uinput(EV_KEY, BTN_TOUCH, 0);

    emit_uinput(EV_SYN, SYN_REPORT, 0);
}

void cleanup_input()
{
    if (uinput_fd >= 0)
    {
        reset_input_state();
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        uinput_fd = -1;
    }
}
