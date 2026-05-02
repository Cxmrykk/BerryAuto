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

get_modeline() {
    local res="${1}x${2}"
    case "$res" in
        "800x480")   echo '"800x480_60.00" 29.50 800 824 896 992 480 483 493 500 -hsync +vsync' ;;
        "1280x720")  echo '"1280x720_60.00" 74.50 1280 1344 1472 1664 720 723 728 748 -hsync +vsync' ;;
        "1920x1080") echo '"1920x1080_60.00" 173.00 1920 2048 2248 2576 1080 1083 1088 1120 -hsync +vsync' ;;
        *) echo "" ;;
    esac
}

if ! run_x11 pgrep -x pipewire > /dev/null; then
    echo "[RESIZE] PipeWire is down! Attempting to start it..."
    run_x11 pipewire &
    sleep 2
fi

if [ -n "$DETECTED_WAYLAND_DISPLAY" ]; then
    if ! command -v wlr-randr >/dev/null 2>&1; then
        echo "[RESIZE] WARNING: Wayland detected but wlr-randr is missing! Please run: sudo apt install wlr-randr"
    elif run_wayland wlr-randr >/dev/null 2>&1; then
        echo "[RESIZE] Detected Wayland Environment..."
        OUTPUT=$(run_wayland wlr-randr | grep "^[^ ]" | head -n 1 | awk '{print $1}')
        if [ -n "$OUTPUT" ]; then
            run_wayland wlr-randr --output "$OUTPUT" --custom-mode "${W}x${H}"
            echo "[RESIZE] Wayland Desktop successfully adjusted!"
            exit 0
        fi
    fi
fi

if command -v xrandr >/dev/null 2>&1 && run_x11 xrandr >/dev/null 2>&1; then
    echo "[RESIZE] Detected X11 Environment..."
    XRANDR_OUT=$(run_x11 xrandr)
    
    OUTPUT=$(echo "$XRANDR_OUT" | grep " connected" | head -n 1 | awk '{print $1}')
    if [ -z "$OUTPUT" ]; then OUTPUT=$(echo "$XRANDR_OUT" | grep "primary" | head -n 1 | awk '{print $1}'); fi
    if [ -z "$OUTPUT" ]; then OUTPUT=$(echo "$XRANDR_OUT" | grep "default" | head -n 1 | awk '{print $1}'); fi
    if [ -z "$OUTPUT" ]; then OUTPUT=$(echo "$XRANDR_OUT" | grep -E "^(HDMI|DP|VGA|DVI|Virtual|XWAYLAND)" | head -n 1 | awk '{print $1}'); fi
    
    # Use a highly specific mode name to prevent regex collisions
    MODE_NAME="${W}x${H}_AA"
    
    if ! echo "$XRANDR_OUT" | grep -q -w "$MODE_NAME"; then
        MODE_INFO=$(get_modeline "$W" "$H")
        MODE_PARAMS=$(echo "$MODE_INFO" | cut -d' ' -f2-)
        run_x11 xrandr --newmode "$MODE_NAME" $MODE_PARAMS
        run_x11 xrandr --addmode "$OUTPUT" "$MODE_NAME"
    fi

    run_x11 xrandr --output "$OUTPUT" --mode "$MODE_NAME"
    echo "[RESIZE] X11 Desktop successfully adjusted to $MODE_NAME!"

else
    echo "[RESIZE] ERROR: Desktop environment not accessible!"
    exit 1
fi
