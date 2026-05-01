#!/bin/bash
W=$1
H=$2

# 1. Safely locate the active Desktop User and their Environment (Bypass root isolation)
REAL_USER=$(who | grep -E '(:0|tty7|wayland)' | awk '{print $1}' | head -n 1)
if [ -z "$REAL_USER" ]; then
    REAL_USER=${SUDO_USER:-$USER}
fi

USER_UID=$(id -u "$REAL_USER")
USER_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)

export XDG_RUNTIME_DIR="/run/user/$USER_UID"
export WAYLAND_DISPLAY="wayland-1"
export DISPLAY=":0"
export XAUTHORITY="$USER_HOME/.Xauthority"

echo "[RESIZE] Adapting Pi Desktop to ${W}x${H} for user '$REAL_USER'..."

# 2. Wayland Handling
if command -v wlr-randr >/dev/null 2>&1 && wlr-randr >/dev/null 2>&1; then
    echo "[RESIZE] Detected Wayland Environment..."
    OUTPUT=$(wlr-randr | grep "^[^ ]" | head -n 1 | awk '{print $1}')
    
    if [ -n "$OUTPUT" ]; then
        wlr-randr --output "$OUTPUT" --custom-mode "${W}x${H}"
        echo "[RESIZE] Wayland Desktop successfully adjusted!"
    else
        echo "[RESIZE] ERROR: Failed to detect Wayland output."
    fi

# 3. X11 Handling
elif command -v xrandr >/dev/null 2>&1; then
    echo "[RESIZE] Detected X11 Environment..."
    
    # Grab the connected output (e.g. HDMI-1, HDMI-A-1)
    OUTPUT=$(xrandr | grep -w "connected" | head -n 1 | awk '{print $1}')
    
    if [ -z "$OUTPUT" ]; then
        echo "[RESIZE] ERROR: Failed to detect X11 output. Is the display connected?"
        exit 1
    fi

    # Check if the requested resolution already exists
    if ! xrandr | grep -q -w "${W}x${H}"; then
        echo "[RESIZE] Resolution ${W}x${H} not found in EDID. Generating custom Modeline..."
        
        # Generate the mode parameters using cvt
        MODELINE_STR=$(cvt "$W" "$H" 60 | grep Modeline)
        MODE_INFO=$(echo "$MODELINE_STR" | sed 's/.*Modeline //')
        
        # Extract the name (e.g., "800x480_60.00") and the timings
        MODE_NAME=$(echo "$MODE_INFO" | awk '{print $1}' | tr -d '"')
        MODE_PARAMS=$(echo "$MODE_INFO" | cut -d' ' -f2-)
        
        # Inject the new mode into X11
        xrandr --newmode "$MODE_NAME" $MODE_PARAMS
        xrandr --addmode "$OUTPUT" "$MODE_NAME"
    else
        # The mode exists natively, just grab its literal name
        MODE_NAME=$(xrandr | grep -w "${W}x${H}" | head -n 1 | awk '{print $1}')
    fi

    # Apply the resolution
    xrandr --output "$OUTPUT" --mode "$MODE_NAME"
    echo "[RESIZE] X11 Desktop successfully adjusted!"

else
    echo "[RESIZE] ERROR: Neither wlr-randr nor xrandr found or accessible!"
    exit 1
fi
