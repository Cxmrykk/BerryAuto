#!/bin/bash
cleanup() {
    echo "[RUNNER] Shutting down..."
    sudo pkill -9 opengal_emitter
    sleep 1
    if [ -d "/sys/kernel/config/usb_gadget/opengal" ]; then
        echo "" | sudo tee "/sys/kernel/config/usb_gadget/opengal/UDC" > /dev/null 2>&1
    fi
    exit
}
trap cleanup SIGINT

sudo pkill -9 opengal_emitter 2>/dev/null || true

CURRENT_USER=${SUDO_USER:-$USER}
USER_UID=$(id -u "$CURRENT_USER")
USER_HOME=$(getent passwd "$CURRENT_USER" | cut -d: -f6)

export DISPLAY=:0
export XAUTHORITY="$USER_HOME/.Xauthority"
export XDG_RUNTIME_DIR="/run/user/$USER_UID"

if [ -z "$WAYLAND_DISPLAY" ]; then
    if [ -S "$XDG_RUNTIME_DIR/wayland-1" ]; then
        export WAYLAND_DISPLAY="wayland-1"
    elif [ -S "$XDG_RUNTIME_DIR/wayland-0" ]; then
        export WAYLAND_DISPLAY="wayland-0"
    fi
fi

xhost +SI:localuser:root > /dev/null 2>&1 || true

sudo /usr/local/bin/setup_opengal_gadget.sh
echo "HU-AAAAAA001" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/serialnumber > /dev/null

echo "[RUNNER] Native Wayland Environment: WAYLAND=$WAYLAND_DISPLAY"

./berryautod/build/opengal_emitter &
EMITTER_PID=$!

sleep 2
UDC_NAME=$(ls /sys/class/udc | head -n 1)
if [ -n "$UDC_NAME" ]; then
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
else
    kill $EMITTER_PID
    exit 1
fi

wait $EMITTER_PID
EXIT_CODE=$?

if [ $EXIT_CODE -eq 42 ]; then
    echo "[BOUNCE] AOA Start received! Morphing to Accessory Mode..."
    sudo sh -c "echo '' > /sys/kernel/config/usb_gadget/opengal/UDC" 2>/dev/null || true
    sleep 1 
    
    echo 0x2D00 | sudo tee /sys/kernel/config/usb_gadget/opengal/idProduct > /dev/null
    echo 0 | sudo tee /sys/kernel/config/usb_gadget/opengal/bDeviceClass > /dev/null
    echo "Android" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/manufacturer > /dev/null
    echo "Android Auto" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/product > /dev/null
    
    echo "[RUNNER] Restarting for AAP Stream..."
    ./berryautod/build/opengal_emitter &
    PID2=$!
    
    sleep 2 
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    wait $PID2
fi
