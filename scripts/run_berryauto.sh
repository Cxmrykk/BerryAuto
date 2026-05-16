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

# Kill any old ghost root processes from previous runs
sudo pkill -9 opengal_emitter 2>/dev/null || true

# Ensure EVDI kernel module is loaded
sudo modprobe evdi 2>/dev/null || true

# Check if any virtual screens exist. If not, tell the kernel to create one.
EVDI_COUNT=$(cat /sys/devices/evdi/count 2>/dev/null || echo 0)
if [ "$EVDI_COUNT" -eq 0 ] && [ -f /sys/devices/evdi/add ]; then
    echo "[RUNNER] Provisioning kernel EVDI virtual display..."
    echo 1 | sudo tee /sys/devices/evdi/add > /dev/null
    # Give udev a split second to set the /dev/dri/cardX permissions for the 'video' group
    sleep 1 
fi

sudo /usr/local/bin/setup_opengal_gadget.sh
echo "HU-AAAAAA001" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/serialnumber > /dev/null

echo "[RUNNER] Starting BerryAuto Daemon..."

systemd-run --quiet --user --scope --unit="app-com.berryauto.receiver-$$" "$PWD/berryautod/build/opengal_emitter" &
EMITTER_PID=$!

echo "[RUNNER] Waiting for USB Device Controller (UDC) to become available..."
# Poll for the UDC for up to 30 seconds
for i in {1..30}; do
    UDC_NAME=$(ls /sys/class/udc 2>/dev/null | head -n 1)
    if [ -n "$UDC_NAME" ]; then
        break
    fi
    sleep 1
done

if [ -n "$UDC_NAME" ]; then
    echo "[RUNNER] Found UDC: $UDC_NAME. Evicting any existing system processes..."
    for u in /sys/kernel/config/usb_gadget/*/UDC; do
        if [ "$(cat "$u" 2>/dev/null)" = "$UDC_NAME" ]; then
            echo "" | sudo tee "$u" >/dev/null 2>&1
        fi
    done
    sudo modprobe -r g_serial g_ether g_mass_storage g_multi 2>/dev/null || true
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
    
    systemd-run --quiet --user --scope --unit="app-com.berryauto.receiver-morph-$$" "$PWD/berryautod/build/opengal_emitter" &
    PID2=$!
    
    sleep 2 
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    wait $PID2
fi
