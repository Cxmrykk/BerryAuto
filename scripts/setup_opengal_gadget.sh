#!/bin/bash
modprobe libcomposite

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
echo 0x0200 > bcdUSB

mkdir -p strings/0x409
cat /proc/sys/kernel/random/uuid | tr -d '-' > strings/0x409/serialnumber
echo "Google" > strings/0x409/manufacturer
echo "Pixel 6" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "Conf" > configs/c.1/strings/0x409/configuration
echo 500 > configs/c.1/MaxPower

mkdir -p functions/ffs.opengal
ln -s functions/ffs.opengal configs/c.1/

mkdir -p /dev/ffs-opengal
mount -t functionfs opengal /dev/ffs-opengal