#!/bin/bash
cleanup() {
    echo "[RUNNER] Shutting down..."
    sudo pkill -SIGINT opengal_emitter
    sleep 1
    if [ -d "/sys/kernel/config/usb_gadget/opengal" ]; then
        echo "" | sudo tee "/sys/kernel/config/usb_gadget/opengal/UDC" > /dev/null 2>&1
    fi
    exit
}
trap cleanup SIGINT

CURRENT_USER=${SUDO_USER:-$USER}
USER_HOME=$(getent passwd "$CURRENT_USER" | cut -d: -f6)

export DISPLAY=:0
export XAUTHORITY="$USER_HOME/.Xauthority"

# Allow root to access the display
xhost +SI:localuser:root > /dev/null 2>&1 || true

# Wake up the screen and disable sleep/blanking
xset s reset > /dev/null 2>&1 || true
xset s off > /dev/null 2>&1 || true
xset -dpms > /dev/null 2>&1 || true

# 1. Setup the initial gadget (Google Pixel MTP)
sudo /usr/local/bin/setup_opengal_gadget.sh

# 2. Start the daemon in the background
sudo -E ./berryautod/build/opengal_emitter &
EMITTER_PID=$!

# Wait for C++ endpoints to initialize
sleep 2
UDC_NAME=$(ls /sys/class/udc | head -n 1)

if [ -z "$UDC_NAME" ]; then
    echo "[ERROR] No UDC (USB controller) found. Are you using the OTG port?"
    kill $EMITTER_PID
    exit 1
fi

# 3. Bind the gadget -> Car sees a normal phone and probes it
echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
echo "[BOUNCE] Pretending to be a phone. Waiting 4 seconds for car to probe..."
sleep 4

# 4. Unbind the gadget -> Car sees phone disconnect
echo "" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
sleep 1

# 5. Morph the gadget into an Android Auto Accessory
echo "[BOUNCE] Morphing into Android Auto Accessory Mode (0x2D00)..."
echo 0x2D00 | sudo tee /sys/kernel/config/usb_gadget/opengal/idProduct > /dev/null
echo "Android" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/manufacturer > /dev/null
echo "Android Auto" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/product > /dev/null

# 6. Rebind the gadget -> Car sees Android Auto start!
sleep 1
echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
echo "[BOUNCE] Accessory Mode Active! Car should start AAP stream now."

wait $EMITTER_PID