#!/bin/bash

# Function to clean up on exit
cleanup() {
    echo ""
    echo "[RUNNER] Shutting down..."
    # Kill the emitter process
    sudo pkill -SIGINT opengal_emitter
    sleep 1
    # Unbind the USB gadget
    GADGET_DIR="/sys/kernel/config/usb_gadget/opengal"
    if [ -d "$GADGET_DIR" ]; then
        echo "" | sudo tee "$GADGET_DIR/UDC" > /dev/null 2>&1
    fi
    echo "[RUNNER] Cleaned up. Exiting."
    exit
}

# Listen for CTRL+C (SIGINT) and call cleanup
trap cleanup SIGINT

echo "1. Configuring USB Gadget ConfigFS..."
sudo /usr/local/bin/setup_opengal_gadget.sh

echo "2. Starting OpenGAL Emitter in the background..."
sudo ./opengal_emitter &
EMITTER_PID=$!

echo "3. Waiting 2 seconds for FunctionFS..."
sleep 2

echo "4. Binding Gadget to USB Controller..."
UDC_NAME=$(ls /sys/class/udc | head -n 1)
if [ -n "$UDC_NAME" ]; then
    echo "$UDC_NAME" | sudo tee /sys/kernel/config/usb_gadget/opengal/UDC > /dev/null
    echo "SUCCESS: BerryAuto bound to $UDC_NAME"
else
    echo "ERROR: No UDC controller found."
    kill $EMITTER_PID
    exit 1
fi

# Wait for the emitter to finish naturally or via trap
wait $EMITTER_PID