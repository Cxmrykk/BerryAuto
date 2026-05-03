#include "globals.hpp"
#include "video_encoder.hpp"
#include <iostream>

bool VideoEncoder::init_x11()
{
    const char* disp_env = getenv("DISPLAY");
    if (!disp_env)
        setenv("DISPLAY", ":0", 1);

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return false;

    root_window = DefaultRootWindow(dpy);
    img = XShmCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), DefaultDepth(dpy, DefaultScreen(dpy)), ZPixmap,
                          NULL, &shminfo, target_width, target_height);
    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    shminfo.shmaddr = img->data = (char*)shmat(shminfo.shmid, 0, 0);
    shminfo.readOnly = False;
    XShmAttach(dpy, &shminfo);

    pw_w = target_width;
    pw_h = target_height;
    pw_fmt = AV_PIX_FMT_BGRA;
    update_sws();

    return true;
}

void VideoEncoder::cleanup_x11()
{
    if (img)
    {
        XShmDetach(dpy, &shminfo);
        XSync(dpy, False);
        XDestroyImage(img);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, 0);
        img = nullptr;
    }
    if (dpy)
    {
        XCloseDisplay(dpy);
        dpy = nullptr;
    }
}
