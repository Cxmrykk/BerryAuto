#!/bin/bash
W=$1
H=$2

# 1. Safely locate the active Desktop User
REAL_USER=$(who | grep -E '(:0|tty7|wayland)' | awk '{print $1}' | head -n 1)
if [ -z "$REAL_USER" ]; then
    REAL_USER=${SUDO_USER:-$USER}
fi

USER_UID=$(id -u "$REAL_USER")
USER_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)

echo "[RESIZE] Adapting Pi Desktop to ${W}x${H} for user '$REAL_USER'..."

DETECTED_WAYLAND_DISPLAY=""
if [ -S "/run/user/$USER_UID/wayland-1" ]; then
    DETECTED_WAYLAND_DISPLAY="wayland-1"
elif [ -S "/run/user/$USER_UID/wayland-0" ]; then
    DETECTED_WAYLAND_DISPLAY="wayland-0"
fi

run_wayland() {
    sudo -u "$REAL_USER" env XDG_RUNTIME_DIR="/run/user/$USER_UID" WAYLAND_DISPLAY="$DETECTED_WAYLAND_DISPLAY" "$@"
}

run_x11() {
    sudo -u "$REAL_USER" env XDG_RUNTIME_DIR="/run/user/$USER_UID" DISPLAY=":0" XAUTHORITY="$USER_HOME/.Xauthority" "$@"
}

if ! run_x11 pgrep -x pipewire > /dev/null; then
    run_x11 pipewire &
    sleep 2
fi

if [ -n "$DETECTED_WAYLAND_DISPLAY" ]; then
    # Wayland Path: Check for Rust gnome-randr (GNOME/Mutter)
    if command -v gnome-randr >/dev/null 2>&1; then
        echo "[RESIZE] Detected GNOME Wayland Environment (Rust gnome-randr)..."
        
        # Query active monitors and extract the last column (the connector name)
        OUTPUT=$(run_wayland gnome-randr query --listactivemonitors | grep -v "Monitors:" | head -n 1 | awk '{print $NF}')
        
        if [ -n "$OUTPUT" ]; then
            echo "[RESIZE] Changing resolution of $OUTPUT to ${W}x${H}..."
            
            # Attempt to set resolution, catch errors if the kernel forbids it
            if run_wayland gnome-randr modify "$OUTPUT" --mode "${W}x${H}" >/dev/null 2>&1; then
                echo "[RESIZE] GNOME Desktop successfully adjusted!"
            else
                echo "[RESIZE] WARNING: Resolution ${W}x${H} is not permitted by the kernel!"
                echo "[RESIZE] The image will be safely upscaled, but aspect ratios may differ."
                echo "[RESIZE] -> To fix this natively, edit: /boot/firmware/cmdline.txt"
                echo "[RESIZE] -> Change your video parameter to: video=${OUTPUT}:${W}x${H}@60e"
            fi
            exit 0
        fi
        
    # Fallback to wlroots (Wayfire)
    elif run_wayland /usr/bin/wlr-randr >/dev/null 2>&1; then
        echo "[RESIZE] Detected wlroots Wayland Environment..."
        OUTPUT=$(run_wayland /usr/bin/wlr-randr | grep "^[^ ]" | head -n 1 | awk '{print $1}')
        if [ -n "$OUTPUT" ]; then
            run_wayland /usr/bin/wlr-randr --output "$OUTPUT" --custom-mode "${W}x${H}"
            echo "[RESIZE] Wayland Desktop successfully adjusted!"
            exit 0
        fi
    fi
fi

# X11 Fallback Path
if command -v xrandr >/dev/null 2>&1 && run_x11 xrandr >/dev/null 2>&1; then
    echo "[RESIZE] Detected X11 Environment. Leaving resolution alone as X11 handles dynamic scaling better."
    exit 0
else
    echo "[RESIZE] ERROR: Desktop environment not accessible!"
    exit 1
fi
