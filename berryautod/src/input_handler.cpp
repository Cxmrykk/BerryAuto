#include "input_handler.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int uinput_fd = -1;

static void emit_event(int fd, uint16_t type, uint16_t code, int32_t val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    // Note: The kernel will automatically populate the timestamp
    write(fd, &ie, sizeof(ie));
}

static bool init_uinput(int max_x, int max_y)
{
    if (uinput_fd >= 0)
        return true;

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0)
    {
        LOG_E("Failed to open /dev/uinput. Ensure the uinput kernel module is loaded and daemon is root.");
        return false;
    }

    // 1. Allow emulating Touch and Left Mouse Button
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);

    // 2. Allow emitting Absolute X and Y coordinates
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);

    // 3. CRUCIAL: Tell libinput this is a Touchscreen, NOT a Touchpad
    ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "BerryAuto Virtual Touch");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    // Set the absolute min/max limits for the touchscreen
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = max_x;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = max_y;

    if (write(uinput_fd, &uidev, sizeof(uidev)) < 0)
    {
        LOG_E("Failed to write to uinput device.");
        close(uinput_fd);
        uinput_fd = -1;
        return false;
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0)
    {
        LOG_E("Failed to create uinput device.");
        close(uinput_fd);
        uinput_fd = -1;
        return false;
    }

    LOG_I("Successfully created /dev/uinput virtual touch device.");
    return true;
}

void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report)
{
    std::lock_guard<std::recursive_mutex> lock(aap_mutex);

    if (report.has_touch_event() && video_streamer != nullptr)
    {
        int action = report.touch_event().action();

        // Initialize uinput dynamically on the first touch event
        if (uinput_fd < 0)
        {
            if (!init_uinput(video_streamer->get_desktop_width(), video_streamer->get_desktop_height()))
            {
                return;
            }
        }

        // 1. Process UP actions IMMEDIATELY (These often don't contain X/Y coordinates!)
        if (action == 1 || action == 6) // TOUCH_ACTION_UP or TOUCH_ACTION_POINTER_UP
        {
            if (uinput_fd >= 0)
            {
                emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 0);
                emit_event(uinput_fd, EV_KEY, BTN_LEFT, 0);
                emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
            }
            return;
        }

        // 2. Safety guard for DOWN and MOVE actions (which MUST have coordinates)
        if (report.touch_event().pointer_data_size() == 0)
            return;

        int x = report.touch_event().pointer_data(0).x();
        int y = report.touch_event().pointer_data(0).y();

        // Map raw phone coordinates (Touch Space) -> Stream Coordinates (Video Space)
        float video_x = (float)x * global_video_width / global_touch_width;
        float video_y = (float)y * global_video_height / global_touch_height;

        // Remove the margin offset (Inside Video Space)
        float local_x = video_x - video_streamer->get_offset_x();
        float local_y = video_y - video_streamer->get_offset_y();

        int mapped_x = 0;
        int mapped_y = 0;

        // Map valid interior pixel touches over to the Pi's internal desktop space
        if (video_streamer->get_scaled_w() > 0 && video_streamer->get_scaled_h() > 0)
        {
            mapped_x = (int)((local_x / video_streamer->get_scaled_w()) * video_streamer->get_desktop_width());
            mapped_y = (int)((local_y / video_streamer->get_scaled_h()) * video_streamer->get_desktop_height());
        }

        // Clip constraints (ignore accidental touches in the black bar zones)
        if (mapped_x < 0)
            mapped_x = 0;
        if (mapped_y < 0)
            mapped_y = 0;
        if (mapped_x > video_streamer->get_desktop_width())
            mapped_x = video_streamer->get_desktop_width();
        if (mapped_y > video_streamer->get_desktop_height())
            mapped_y = video_streamer->get_desktop_height();

        if (uinput_fd >= 0)
        {
            if (action == 0 || action == 5) // TOUCH_ACTION_DOWN or TOUCH_ACTION_POINTER_DOWN
            {
                emit_event(uinput_fd, EV_ABS, ABS_X, mapped_x);
                emit_event(uinput_fd, EV_ABS, ABS_Y, mapped_y);
                emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 1);
                emit_event(uinput_fd, EV_KEY, BTN_LEFT, 1);
                emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
            }
            else if (action == 2) // TOUCH_ACTION_MOVE
            {
                emit_event(uinput_fd, EV_ABS, ABS_X, mapped_x);
                emit_event(uinput_fd, EV_ABS, ABS_Y, mapped_y);
                emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
            }
        }
    }
}

void cleanup_input()
{
    if (uinput_fd >= 0)
    {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        uinput_fd = -1;
        LOG_I("Destroyed /dev/uinput virtual touch device.");
    }
}
