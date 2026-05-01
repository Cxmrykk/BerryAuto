#!/bin/bash
echo "[BOUNCE] C++ triggered Morph to Android Auto Accessory Mode (0x2D00)..."
UDC_NAME=$(ls /sys/class/udc | head -n 1)

# Unbind
echo "" > /sys/kernel/config/usb_gadget/opengal/UDC

# Morph to Accessory Mode
echo 0x2D00 > /sys/kernel/config/usb_gadget/opengal/idProduct
echo "Android" > /sys/kernel/config/usb_gadget/opengal/strings/0x409/manufacturer
# Note: Real phones become "Android Accessory", not "Android Auto"
echo "Android Accessory" > /sys/kernel/config/usb_gadget/opengal/strings/0x409/product

# Rebind
sleep 0.5
echo "$UDC_NAME" > /sys/kernel/config/usb_gadget/opengal/UDC
echo "[BOUNCE] Accessory Mode Active!"