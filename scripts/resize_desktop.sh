#!/bin/bash
W=$1
H=$2

export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export WAYLAND_DISPLAY="wayland-1"
export DISPLAY=":0"

echo "[RESIZE] Adapting Pi Desktop to ${W}x${H}..."

if command -v wlr-randr >/dev/null 2>&1 && wlr-randr >/dev/null 2>&1; then
    # Wayland Mode (Bookworm / Wayfire)
    OUTPUT=$(wlr-randr | grep "^[^ ]" | head -n 1 | awk '{print $1}')
    wlr-randr --output "$OUTPUT" --custom-mode "${W}x${H}"
elif command -v xrandr >/dev/null 2>&1; then
    # X11 Mode (Legacy / Bullseye)
    OUTPUT=$(xrandr | grep " connected" | awk '{print $1}')
    xrandr --output "$OUTPUT" --mode "${W}x${H}"
else
    echo "[RESIZE] ERROR: Neither wlr-randr nor xrandr found or accessible!"
    exit 1
fi

echo "[RESIZE] Desktop successfully adjusted!"
