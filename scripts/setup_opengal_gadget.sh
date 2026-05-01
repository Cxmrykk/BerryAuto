#!/bin/bash
modprobe libcomposite
modprobe uinput

GADGET_DIR="/sys/kernel/config/usb_gadget/opengal"

if [ -d "$GADGET_DIR" ]; then
    cd "$GADGET_DIR" || exit
    echo "" > UDC 2>/dev/null || true
    rm -f configs/c.1/ffs.opengal 2>/dev/null || true
    rmdir configs/c.1/strings/0x409 2>/dev/null || true
    rmdir configs/c.1 2>/dev/null || true
    rmdir functions/ffs.opengal 2>/dev/null || true
    rmdir strings/0x409 2>/dev/null || true
    cd ..
    rmdir "$GADGET_DIR" 2>/dev/null || true
fi

umount /dev/ffs-opengal 2>/dev/null || true
rm -rf /dev/ffs-opengal 2>/dev/null || true

mkdir -p "$GADGET_DIR"
cd "$GADGET_DIR" || exit

# 1. Start disguised as a normal Google Pixel (MTP)
echo 0x18D1 > idVendor
echo 0x4EE1 > idProduct
echo 0x0200
