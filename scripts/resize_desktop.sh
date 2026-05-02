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
        echo "[RESIZE] Resolution ${W}x${H} not found in EDID. Generating custom Modeline..."
        
        # Generate the mode parameters using cvt
        MODELINE_STR=$(cvt "$W" "$H" 60 | grep Modeline)
        MODE_INFO=$(echo "$MODELINE_STR" | sed 's/.*Modeline //')
        
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
