#pragma once

extern "C"
{
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
}

// Prevent X11 macros from breaking C++ and Protobuf
#undef Status
#undef None
#undef Bool