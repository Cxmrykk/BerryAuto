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

sudo /usr/local/bin/setup_opengal_gadget.sh
echo "HU-AAAAAA001" | sudo tee /sys/kernel/config/usb_gadget/opengal/strings/0x409/serialnumber > /dev/null

echo "[RUNNER] Starting BerryAuto Daemon..."

systemd-run --quiet --user --scope --unit="app-com.berryauto.receiver-$$" "$PWD/berryautod/build/opengal_emitter" &
EMITTER_PID=$!

sleep 2
UDC_NAME=$(ls /sys/class/udc | head -n 1)
if [ -n "$UDC_NAME" ]; then
    echo "[RUNNER] Evicting any system processes holding UDC: $UDC_NAME"
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
