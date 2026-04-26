#!/bin/bash
modprobe libcomposite

GADGET_DIR="/sys/kernel/config/usb_gadget/opengal"

# --- 1. CLEANUP PREVIOUS STATE ---
if [ -d "$GADGET_DIR" ]; then
    echo "[SCRIPT] Cleaning up existing OpenGAL gadget..."
    cd "$GADGET_DIR" || exit
    
    # Unbind from USB controller
    echo "" > UDC 2>/dev/null || true
    
    # Remove symlinks and directories in reverse order of creation
    rm -f configs/c.1/ffs.opengal 2>/dev/null || true
    rmdir configs/c.1/strings/0x409 2>/dev/null || true
    rmdir configs/c.1 2>/dev/null || true
    rmdir functions/ffs.opengal 2>/dev/null || true
    rmdir strings/0x409 2>/dev/null || true
    
    cd ..
    rmdir "$GADGET_DIR" 2>/dev/null || true
fi

# Unmount functionfs if it was left mounted
umount /dev/ffs-opengal 2>/dev/null || true
rm -rf /dev/ffs-opengal 2>/dev/null || true

# --- 2. SETUP NEW STATE ---
echo "[SCRIPT] Setting up OpenGAL gadget..."
mkdir -p "$GADGET_DIR"
cd "$GADGET_DIR" || exit

echo 0x18D1 > idVendor
echo 0x2D00 > idProduct
echo 0x0200 > bcdUSB

mkdir -p strings/0x409
echo "123456789" > strings/0x409/serialnumber
echo "Google" > strings/0x409/manufacturer
echo "Android" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "Projection" > configs/c.1/strings/0x409/configuration

mkdir -p functions/ffs.opengal
ln -s functions/ffs.opengal configs/c.1/

mkdir -p /dev/ffs-opengal
mount -t functionfs opengal /dev/ffs-opengal

echo "[SCRIPT] FunctionFS mounted. Waiting for daemon to start..."