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

# --- Execution Wrappers (Runs commands safely as the desktop user) ---
run_wayland() {
    sudo -u "$REAL_USER" env XDG_RUNTIME_DIR="/run/user/$USER_UID" WAYLAND_DISPLAY="wayland-1" "$@"
}

run_x11() {
    sudo -u "$REAL_USER" env DISPLAY=":0" XAUTHORITY="$USER_HOME/.Xauthority" "$@"
}
# ---------------------------------------------------------------------

# Hardcoded VESA CVT Modelines for all 9 official Android Auto resolutions
get_modeline() {
    local res="${1}x${2}"
    case "$res" in
        "800x480")   echo '"800x480_60.00" 29.50 800 824 896 992 480 483 493 500 -hsync +vsync' ;;
        "1280x720")  echo '"1280x720_60.00" 74.50 1280 1344 1472 1664 720 723 728 748 -hsync +vsync' ;;
        "1920x1080") echo '"1920x1080_60.00" 173.00 1920 2048 2248 2576 1080 1083 1088 1120 -hsync +vsync' ;;
        "2560x1440") echo '"2560x1440_60.00" 311.83 2560 2744 3024 3488 1440 1443 1448 1490 -hsync +vsync' ;;
        "3840x2160") echo '"3840x2160_60.00" 712.75 3840 4104 4512 5216 2160 2163 2168 2277 -hsync +vsync' ;;
        "720x1280")  echo '"720x1280_60.00" 74.50 720 760 832 944 1280 1283 1293 1314 -hsync +vsync' ;;
        "1080x1920") echo '"1080x1920_60.00" 173.00 1080 1144 1256 1432 1920 1923 1933 2018 -hsync +vsync' ;;
        "1440x2560") echo '"1440x2560_60.00" 311.83 1440 1536 1688 1936 2560 2563 2573 2686 -hsync +vsync' ;;
        "2160x3840") echo '"2160x3840_60.00" 712.75 2160 2312 2544 2928 3840 3843 3853 4060 -hsync +vsync' ;;
        *) echo "" ;;
    esac
}

# 2. Wayland Handling
if command -v wlr-randr >/dev/null 2>&1 && run_wayland wlr-randr >/dev/null 2>&1; then
    echo "[RESIZE] Detected Wayland Environment..."
    OUTPUT=$(run_wayland wlr-randr | grep "^[^ ]" | head -n 1 | awk '{print $1}')
    
    if [ -n "$OUTPUT" ]; then
        run_wayland wlr-randr --output "$OUTPUT" --custom-mode "${W}x${H}"
        echo "[RESIZE] Wayland Desktop successfully adjusted!"
    else
        echo "[RESIZE] ERROR: Failed to detect Wayland output."
    fi

# 3. X11 Handling
elif command -v xrandr >/dev/null 2>&1 && run_x11 xrandr >/dev/null 2>&1; then
    echo "[RESIZE] Detected X11 Environment..."
    
    # Capture the raw output of xrandr to safely parse
    XRANDR_OUT=$(run_x11 xrandr)
    
    # 1. Look for a physically connected display
    OUTPUT=$(echo "$XRANDR_OUT" | grep " connected" | head -n 1 | awk '{print $1}')
    
    # 2. Headless Fallback: Look for the 'primary' display (even if disconnected)
    if [ -z "$OUTPUT" ]; then
        OUTPUT=$(echo "$XRANDR_OUT" | grep "primary" | head -n 1 | awk '{print $1}')
    fi
    
    # 3. Headless Fallback: Look for a 'default' display frame buffer
    if [ -z "$OUTPUT" ]; then
        OUTPUT=$(echo "$XRANDR_OUT" | grep "default" | head -n 1 | awk '{print $1}')
    fi
    
    # 4. Absolute Fallback: Just grab the first HDMI/DP interface listed
    if [ -z "$OUTPUT" ]; then
        OUTPUT=$(echo "$XRANDR_OUT" | grep -E "^(HDMI|DP|VGA|DVI|Virtual|XWAYLAND)" | head -n 1 | awk '{print $1}')
    fi
    
    if [ -z "$OUTPUT" ]; then
        echo "[RESIZE] ERROR: Failed to detect ANY X11 output. Raw xrandr output:"
        echo "$XRANDR_OUT"
        exit 1
    fi

    echo "[RESIZE] Target Output: $OUTPUT"

    # Check if the requested resolution already exists in the list
    if ! echo "$XRANDR_OUT" | grep -q -w "${W}x${H}"; then
        echo "[RESIZE] Resolution ${W}x${H} not found in EDID. Injecting pre-calculated Modeline..."
        
        MODE_INFO=$(get_modeline "$W" "$H")
        if [ -z "$MODE_INFO" ]; then
            echo "[RESIZE] ERROR: Resolution ${W}x${H} is not a standard Android Auto configuration!"
            exit 1
        fi
        
        # Extract the name (e.g., "800x480_60.00") and the timings
        MODE_NAME=$(echo "$MODE_INFO" | awk '{print $1}' | tr -d '"')
        MODE_PARAMS=$(echo "$MODE_INFO" | cut -d' ' -f2-)
        
        # Inject the new mode into X11
        run_x11 xrandr --newmode "$MODE_NAME" $MODE_PARAMS
        run_x11 xrandr --addmode "$OUTPUT" "$MODE_NAME"
    else
        # The mode exists natively, just grab its exact name representation
        MODE_NAME=$(echo "$XRANDR_OUT" | grep -w "${W}x${H}" | head -n 1 | awk '{print $1}')
    fi

    # Apply the resolution
    run_x11 xrandr --output "$OUTPUT" --mode "$MODE_NAME"
    echo "[RESIZE] X11 Desktop successfully adjusted to ${W}x${H}!"

else
    echo "[RESIZE] ERROR: Desktop environment not accessible for user $REAL_USER!"
    echo "Make sure the GUI is running."
    exit 1
fi