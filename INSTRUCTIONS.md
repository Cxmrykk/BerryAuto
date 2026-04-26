# BerryAuto: Native OpenGAL Android Auto Emitter

BerryAuto turns your Raspberry Pi into a native Android Auto external display. It creates a virtual monitor (VKMS), captures the Linux Desktop Environment using zero-copy DRM, encodes it to H.264 using hardware acceleration, and streams it to your car's head unit over a trusted TLS 1.2 Android Auto connection.

## Prerequisites

1. **Hardware:** Raspberry Pi 4 Model B or Raspberry Pi Zero 2 W (must support USB OTG).
2. **Cable:** A high-quality USB Data cable.
   - _Pi 4:_ Use the USB-C port.
   - _Pi Zero 2 W:_ Use the inner micro-USB port marked "USB".
3. **OS:** Flash an SD card with **Raspberry Pi OS (64-bit) with Desktop**.

---

## Step 1: Switch to X11 (Disable Wayland)

Modern Raspberry Pi OS defaults to Wayland, whose security model blocks the zero-copy GPU screen capture our high-performance encoder uses. We must switch back to X11.

1. Open a terminal and launch the Raspberry Pi configuration tool:
   ```bash
   sudo raspi-config
   ```
2. Navigate to **Advanced Options** -> **Wayland**.
3. Select **X11** (or "Openbox / X11").
4. Exit the tool and **Reboot** when prompted.

---

## Step 2: Install Build Dependencies

Install the required compilers, cryptography libraries, protobuf tools, and hardware DRM/V4L2 headers:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config git \
    libssl-dev libprotobuf-dev protobuf-compiler \
    libdrm-dev libavcodec-dev libavutil-dev libevdev-dev
```

---

## Step 3: Configure the Linux Kernel (VKMS & USB OTG)

We need to instruct the Raspberry Pi kernel to enable USB Gadget mode (DWC2) and the Virtual Kernel Mode Setting (VKMS) dummy display.

1. Open the boot config file:

   ```bash
   sudo nano /boot/firmware/config.txt
   ```

   _(Note: On older OS versions, this is `/boot/config.txt`)_

2. Add the following lines to the very bottom:
   ```ini
   # Enable USB OTG Support
   dtoverlay=dwc2,dr_mode=peripheral
   # Enable Virtual Display (VKMS)
   dtoverlay=vkms
   ```
3. Tell the kernel to load the required modules at boot:
   ```bash
   sudo nano /etc/modules
   ```
4. Add these lines to the bottom:
   ```text
   dwc2
   libcomposite
   vkms
   uinput
   ```

---

## Step 4: Configure X11 & USB Gadget Scripts

1. **Force X11 to use the Virtual Display:**
   Copy the provided X11 configuration file so the desktop renders to the virtual monitor instead of the physical HDMI port.

   ```bash
   sudo mkdir -p /etc/X11/xorg.conf.d/
   sudo cp config/10-vkms.conf /etc/X11/xorg.conf.d/
   ```

2. **Install the USB Gadget Script:**
   This script makes the Pi disguise itself as a Google Android Auto Accessory (`VID: 0x18D1`, `PID: 0x2D00`).
   ```bash
   sudo cp scripts/setup_opengal_gadget.sh /usr/local/bin/
   sudo chmod +x /usr/local/bin/setup_opengal_gadget.sh
   ```

**Reboot your Raspberry Pi now so all kernel modules and X11 changes take effect.**

```bash
sudo reboot
```

---

## Step 5: Build the BerryAuto Daemon

Reconnect to your Pi after it reboots. It is now time to compile the C++ `berryautod` application.

1. Navigate to the daemon directory:
   ```bash
   cd ~/BerryAuto/berryautod
   ```
2. Create a build directory and run CMake:
   ```bash
   mkdir build && cd build
   cmake ..
   ```
3. Compile the application (using all available CPU cores):
   ```bash
   make -j$(nproc)
   ```
   _This will generate the protobuf headers, compile the hardware encoder, the TLS stack, and output the `opengal_emitter` binary._

---

## Step 6: Execution & Testing in the Car

You are now ready to project your Linux desktop to your car's head unit.

1. **Plug it in:** Plug your Raspberry Pi into the car's smartphone USB port using your data cable.
2. SSH into your Raspberry Pi.
3. **Initialize the USB Gadget:**
   ```bash
   sudo /usr/local/bin/setup_opengal_gadget.sh
   ```
   _The car may show "Reading USB" or "Android Auto Connected" at this stage._
4. **Start the Daemon:**
   ```bash
   cd ~/BerryAuto/berryautod/build
   sudo ./opengal_emitter
   ```

### Success Verification

Check your console output. You should see the daemon handle the Cleartext Version Request, negotiate the TLS 1.2 Handshake, parse the Service Discovery (SDP) from your car, and start the `DRM -> V4L2` video pipeline.

Look at your car's screen—it should switch from the native infotainment UI over to the Raspberry Pi Desktop. You can now use your car's touchscreen to control the Pi!
