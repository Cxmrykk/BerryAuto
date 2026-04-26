#!/bin/bash

echo "1. Configuring USB Gadget ConfigFS..."
sudo /usr/local/bin/setup_opengal_gadget.sh

echo "2. Starting OpenGAL Emitter in the background..."
# This will open ep0 and provide the descriptors to the kernel
sudo ./opengal_emitter &
EMITTER_PID=$!

echo "3. Waiting 2 seconds for FunctionFS to become ready..."
sleep 2

echo "4. Binding Gadget to USB Controller..."
UDC_NAME=$(ls /sys/class/udc | head -n 1)
if [ -n "$UDC_NAME" ]; then
    sudo sh -c "echo $UDC_NAME > /sys/kernel/config/usb_gadget/opengal/UDC"
    echo "SUCCESS: BerryAuto is bound to $UDC_NAME and ready!"
else
    echo "ERROR: No UDC controller found."
fi

# Bring the C++ daemon back to the foreground so you can see its logs
wait $EMITTER_PID
