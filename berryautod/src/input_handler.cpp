#include "input_handler.hpp"
#include "globals.hpp"
#include "video_encoder.hpp"
#include "x11_wrapper.hpp"

static Display* input_dpy = nullptr;

void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report)
{
    if (report.has_touch_event() && video_streamer != nullptr)
    {
        if (report.touch_event().pointer_data_size() == 0)
            return; // Safety guard

        int action = report.touch_event().action();
        int x = report.touch_event().pointer_data(0).x();
        int y = report.touch_event().pointer_data(0).y();

        // 1. Map raw phone coordinates (Touch Space) -> Stream Coordinates (Video Space)
        float video_x = (float)x * global_video_width / global_touch_width;
        float video_y = (float)y * global_video_height / global_touch_height;

        // 2. Remove the margin offset (Inside Video Space)
        float local_x = video_x - video_streamer->get_offset_x();
        float local_y = video_y - video_streamer->get_offset_y();

        int mapped_x = 0;
        int mapped_y = 0;

        // 3. Map valid interior pixel touches over to the Pi's internal X11 desktop space
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

        if (!input_dpy)
        {
            input_dpy = XOpenDisplay(NULL);
        }

        if (input_dpy)
        {
            if (action == 0 || action == 5)
            {
                XTestFakeMotionEvent(input_dpy, -1, mapped_x, mapped_y, CurrentTime);
                XTestFakeButtonEvent(input_dpy, 1, True, CurrentTime);
            }
            else if (action == 1 || action == 6)
            {
                XTestFakeMotionEvent(input_dpy, -1, mapped_x, mapped_y, CurrentTime);
                XTestFakeButtonEvent(input_dpy, 1, False, CurrentTime);
            }
            else if (action == 2)
            {
                XTestFakeMotionEvent(input_dpy, -1, mapped_x, mapped_y, CurrentTime);
            }
            XFlush(input_dpy);
        }
    }
}

void cleanup_input()
{
    if (input_dpy)
    {
        XCloseDisplay(input_dpy);
        input_dpy = nullptr;
    }
}
