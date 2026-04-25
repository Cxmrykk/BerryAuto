#include "input_injector.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

bool InputInjector::init(int width, int height) {
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) return false;

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);

    struct uinput_abs_setup x_setup = {}; x_setup.code = ABS_X; x_setup.absinfo.maximum = width;
    struct uinput_abs_setup y_setup = {}; y_setup.code = ABS_Y; y_setup.absinfo.maximum = height;

    ioctl(uinput_fd, UI_ABS_SETUP, &x_setup); ioctl(uinput_fd, UI_ABS_SETUP, &y_setup);

    struct uinput_setup usetup = {};
    snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "OpenGAL Touch");
    usetup.id.bustype = BUS_USB;
    ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
    ioctl(uinput_fd, UI_DEV_CREATE);
    return true;
}

void InputInjector::inject_touch(int x, int y, bool is_down) {
    if (uinput_fd < 0) return;
    struct input_event ev[3] = {};
    ev[0].type = EV_ABS; ev[0].code = ABS_X; ev[0].value = x;
    ev[1].type = EV_ABS; ev[1].code = ABS_Y; ev[1].value = y;
    ev[2].type = EV_KEY; ev[2].code = BTN_TOUCH; ev[2].value = is_down ? 1 : 0;
    write(uinput_fd, ev, sizeof(ev));

    struct input_event sync = {};
    sync.type = EV_SYN; sync.code = SYN_REPORT;
    write(uinput_fd, &sync, sizeof(sync));
}

InputInjector::~InputInjector() {
    if (uinput_fd >= 0) { ioctl(uinput_fd, UI_DEV_DESTROY); close(uinput_fd); }
}
