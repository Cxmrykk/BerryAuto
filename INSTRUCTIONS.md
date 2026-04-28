# BerryAuto Raspberry Pi Setup Guide

This guide provides step-by-step instructions to set up your Raspberry Pi from scratch to act as an Android Auto external display device using the **BerryAuto** daemon.

## Prerequisites

- A Raspberry Pi with USB OTG support (e.g., Raspberry Pi 4, Pi Zero W / 2W).
  - _Note: On the Raspberry Pi 4, the USB-C power port is also the data/OTG port._
- A fresh installation of **Raspberry Pi OS (Desktop version)**.
- Internet access on the Pi for initial setup.

---

## Step 1: Switch to X11 (For Bookworm OS and newer)

BerryAuto uses `Xlib` and `XTest` for screen capture and touch injection. Newer versions of Raspberry Pi OS default to Wayland, which will prevent touch inputs and screen grabbing from working properly.

1. Open the terminal and run the config tool:
   ```bash
   sudo raspi-config
   ```
2. Navigate to **Advanced Options** -> **Wayland**.
3. Select **X11** to disable Wayland.
4. Exit and allow the Pi to reboot.

---

## Step 2: Configure the Dummy Resolution (Match your Head Unit)

BerryAuto works by capturing your Pi's desktop and streaming it to your car. To prevent the image from being stretched or having black bars, the Pi's resolution **must** match one of the standard Android Auto resolutions supported by your car's head unit.

1.  **Identify your resolution:** Most modern cars use **720p** (Case 2), but older units may use **480p** (Case 1), and high-end screens may use **1080p** (Case 3).

2.  **Open the boot configuration file:**

    ```bash
    sudo nano /boot/firmware/config.txt
    # (If on an older OS like Bullseye, use /boot/config.txt)
    ```

3.  **Add the settings:** Look for the `[all]` section and add the following. Adjust the `hdmi_cvt` line based on the table below:

    ```ini
    # Force dummy HDMI output even without a monitor
    hdmi_force_hotplug=1
    hdmi_group=2
    hdmi_mode=87

    # --- Resolution Pick List (Uncomment ONLY ONE) ---
    # Case 1: 480p (Standard/Older screens)
    # hdmi_cvt=800 480 60 6 0 0 0

    # Case 2: 720p (Most common - Recommended)
    hdmi_cvt=1280 720 60 6 0 0 0

    # Case 3: 1080p (High-end Wide screens)
    # hdmi_cvt=1920 1080 60 6 0 0 0
    ```

    | Case  | Resolution  | `hdmi_cvt` parameters  | Aspect      |
    | :---- | :---------- | :--------------------- | :---------- |
    | **1** | 800 x 480   | `800 480 60 6 0 0 0`   | 15:9 / 16:9 |
    | **2** | 1280 x 720  | `1280 720 60 6 0 0 0`  | 16:9        |
    | **3** | 1920 x 1080 | `1920 1080 60 6 0 0 0` | 16:9        |

4.  **Enable the USB OTG driver:** Ensure this line is present to allow the Pi to act as a USB device:

    ```ini
    dtoverlay=dwc2,dr_mode=peripheral
    ```

5.  Save (`Ctrl+O`, `Enter`) and Exit (`Ctrl+X`).

> **Tip:** If the car display appears and looks squashed or stretched, return to this step and try a different Case resolution from the table above.

---

## Step 3: Enable Kernel Modules

You must tell the Linux kernel to load the OTG gadget modules at boot.

1. Edit the modules file:
   ```bash
   sudo nano /etc/modules
   ```
2. Add the following two lines to the end of the file:
   ```text
   dwc2
   libcomposite
   ```
3. Save and Exit. **Reboot the Raspberry Pi** now to apply the screen and kernel changes.

---

## Step 4: Install Dependencies & Clone Repository

1. Once the Pi reboots, open a terminal and install the required build tools and libraries:

   ```bash
   sudo apt update
   sudo apt install -y git cmake g++ pkg-config libssl-dev \
       libprotobuf-dev protobuf-compiler libavcodec-dev \
       libavutil-dev libswscale-dev libx11-dev libxext-dev \
       libxrandr-dev libxtst-dev
   ```

2. Clone the repository into your home directory:
   ```bash
   cd ~
   git clone https://github.com/cxmrykk/berryauto.git
   cd berryauto
   ```

---

## Step 5: Build the Daemon

1. Inside the `berryauto` directory, navigate to the daemon folder and build it:
   ```bash
   cd berryautod
   mkdir build
   cd build
   cmake ..
   make -j$(nproc)
   ```
2. Go back to the repository root:
   ```bash
   cd ~/berryauto
   ```

---

## Step 6: Install the Setup Script

The `run_berryauto.sh` script expects the gadget setup script to be located in `/usr/local/bin/`.

1. Copy the setup script and make it executable:
   ```bash
   sudo cp scripts/setup_opengal_gadget.sh /usr/local/bin/
   sudo chmod +x /usr/local/bin/setup_opengal_gadget.sh
   ```
2. Make the main runner script executable as well:
   ```bash
   chmod +x scripts/run_berryauto.sh
   ```

---

## Step 7: Setup Systemd Auto-Start Service

To make BerryAuto start automatically whenever the Pi boots up and the desktop loads, create a systemd service.

1. Grant your user password-less sudo access. _(Note: If your username is not `pi`, replace `pi` with your actual username)._

   ```bash
   echo "pi ALL=(ALL) NOPASSWD: ALL" | sudo tee /etc/sudoers.d/010_pi-nopasswd
   ```

2. Create a new service file:
   ```bash
   sudo nano /etc/systemd/system/berryauto.service
   ```
3. Paste the following configuration. _(Note: If your username is not `pi`, replace `User=pi` and `/home/pi/...` with your actual username and path)._

   ```ini
   [Unit]
   Description=BerryAuto Daemon Service
   After=graphical.target network.target
   Wants=graphical.target

   [Service]
   Type=simple
   User=pi
   WorkingDirectory=/home/pi/berryauto
   ExecStart=/home/pi/berryauto/scripts/run_berryauto.sh
   Restart=always
   RestartSec=5

   [Install]
   WantedBy=graphical.target
   ```

4. Save and Exit.
5. Reload systemd, enable the service, and start it:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable berryauto.service
   sudo systemctl start berryauto.service
   ```

---

## Step 8: Connecting to the Car

You are completely set up!

1. **Powering the Pi:** Plug a high-quality USB cable into the OTG port of your Raspberry Pi (on the Pi 4, this is the USB-C power port).
2. **Connecting:** Plug the other end into the Android Auto capable USB port of your vehicle's Head Unit.
3. The Raspberry Pi will draw power from the car. Once it boots to the desktop, the `berryauto` service will automatically negotiate with the car as an Android Auto device, stream the dummy desktop at the forced resolution, and pass touches back to the Pi!

### Troubleshooting

- **Check Service Logs:** If the car doesn't recognize the device, check the daemon logs by running:
  ```bash
  journalctl -u berryauto.service -f
  ```
- **Check USB Gadget:** Ensure the `ffs` endpoint is successfully mounting in `/dev/ffs-opengal`. If it fails, double-check that `dtoverlay=dwc2` was applied in `config.txt`.
