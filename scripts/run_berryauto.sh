#!/bin/bash
cleanup() {
    echo "[RUNNER] Shutting down..."
    sudo pkill -9 opengal_emitter 2>/dev/null || true
    sleep 1
    if [ -d "/sys/kernel/config/usb_gadget/opengal" ]; then
        sudo sh -c "echo '' > /sys/kernel/config/usb_gadget/opengal/UDC" 2>/dev/null || true
    fi
    exit
}
trap cleanup SIGINT

CURRENT_USER=${SUDO_USER:-$USER}
USER_HOME=$(getent passwd "$CURRENT_USER" | cut -d: -f6)

export DISPLAY=:0
export XAUTHORITY="$USER_HOME/.Xauthority"

xhost +SI:localuser:root > /dev/null 2>&1 || true
xset s reset > /dev/null 2>&1 || true
xset s off > /dev/null 2>&1 || true
xset -dpms > /dev/null 2>&1 || true

# 1. Mount FFS and setup the initial gadget (Google Pixel MTP)
sudo /usr/local/bin/setup_opengal_gadget.sh

# Force the USB serial to match the AOA serial perfectly!
echo "HU-AAAAAA001" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/serialnumber > /dev/null

# 2. Start the daemon in AOA Listener Mode
echo "[RUNNER] Starting Daemon to listen for Car's AOA Probe..."
sudo -E ./berryautod/build/opengal_emitter &
PID1=$!

sleep 1.5
UDC_NAME=$(ls /sys/class/udc | head -n 1)

if [ -z "$UDC_NAME" ]; then
    echo "[ERROR] No UDC found. Are you using the OTG port?"
    kill -9 $PID1
    exit 1
fi

echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null

# 3. Wait for the Daemon to exit. It will exit with code 42 when the car says START.
wait $PID1
EXIT_CODE=$?

if [ $EXIT_CODE -eq 42 ]; then
    echo "[BOUNCE] AOA Start received! Morphing to Accessory Mode..."
    
    # Unbind Gadget (Car sees phone instantly disconnect)
    sudo sh -c "echo '' > /sys/kernel/config/usb_gadget/opengal/UDC" 2>/dev/null || true
    
    # Morph IDs to Accessory
    echo 0x2D00 | sudo tee /sys/kernel/config/usb_gadget/opengal/idProduct > /dev/null
    
    # CRITICAL: Real phones use DeviceClass 0 in Accessory Mode
    echo 0 | sudo tee /sys/kernel/config/usb_gadget/opengal/bDeviceClass > /dev/null
    echo 0 | sudo tee /sys/kernel/config/usb_gadget/opengal/bDeviceSubClass > /dev/null
    echo 0 | sudo tee /sys/kernel/config/usb_gadget/opengal/bDeviceProtocol > /dev/null
    
    echo "Android" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/manufacturer > /dev/null
    echo "Android Auto" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/product > /dev/null
    
    # Restart the Daemon to get fresh endpoints!
    echo "[RUNNER] Restarting Daemon for AAP Stream..."
    sudo -E ./berryautod/build/opengal_emitter &
    PID2=$!
    
    sleep 1
    
    # Rebind Gadget (Car sees Accessory connect)
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    echo "[BOUNCE] Accessory Mode Active!"
    
    wait $PID2
else
    echo "[RUNNER] Daemon exited with code $EXIT_CODE. Stopping."
fi