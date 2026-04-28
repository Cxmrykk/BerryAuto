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

# 1. Mount FFS and setup the initial gadget (Google Pixel MTP)
sudo /usr/local/bin/setup_opengal_gadget.sh

# 2. Start the daemon in AOA Listener Mode
echo "[RUNNER] Starting Daemon to listen for Car's AOA Probe..."
sudo -E ./berryautod/build/opengal_emitter &
PID1=$!

sleep 1.5
UDC_NAME=$(ls /sys/class/udc | head -n 1)

if [ -z "$UDC_NAME" ]; then
    echo "[ERROR] No UDC (USB controller) found. Are you using the OTG port?"
    kill $PID1
    exit 1
fi

echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null

# 3. Wait for the Daemon to exit. It will exit with code 42 when the car says START.
wait $PID1
EXIT_CODE=$?

if [ $EXIT_CODE -eq 42 ]; then
    echo "[BOUNCE] AOA Start received! Morphing to Accessory Mode..."
    
    # Unbind Gadget (Car sees phone disconnect)
    echo "" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    
    # Morph IDs to Accessory
    echo 0x2D00 | sudo tee /sys/kernel/config/usb_gadget/opengal/idProduct > /dev/null
    echo "Android" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/manufacturer > /dev/null
    echo "Android Auto" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/product > /dev/null
    
    # Restart the Daemon to get fresh endpoints!
    echo "[RUNNER] Restarting Daemon for AAP Stream..."
    sudo -E ./berryautod/build/opengal_emitter &
    PID2=$!
    
    sleep 1.5
    # Rebind Gadget (Car sees Accessory connect)
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    echo "[BOUNCE] Accessory Mode Active!"
    
    wait $PID2
else
    echo "[RUNNER] Daemon exited with code $EXIT_CODE. Stopping."
fi