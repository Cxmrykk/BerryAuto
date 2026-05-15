# BerryAuto

BerryAuto is a daemon that transforms a Linux single-board computer into a wired Android Auto receiver. It handles Android Auto Protocol (AAP) communication over USB OTG, acts as a virtual hardware monitor via EVDI, encodes the display to H.264/HEVC, translates Android Auto touch inputs into local Linux virtual input devices, and provides a dedicated virtual soundcard via a custom DKMS kernel module for flawless audio sync.

While the Raspberry Pi 4 is a popular target, **any Linux device** with a USB Device Controller (UDC), hardware video encoding capabilities (V4L2/OMX/VAAPI), and DKMS support is compatible.

## Features

- **USB OTG Emulation:** Uses Linux FunctionFS to negotiate the Android Open Accessory (AOA) protocol.
- **Kernel-Level Virtual Display (EVDI):** Appears to the OS as a physical plug-and-play monitor. Bypasses all Wayland/X11 screen-capture restrictions and headless display problems.
- **Dynamic EDID Injection:** Automatically generates and injects a hardware EDID matching the vehicle's exact resolution and frame rate, causing the Linux desktop to perfectly resize itself natively.
- **Smart Hardware Encoding:** Automatically queries FFmpeg to discover and prioritize system-specific hardware encoders (`v4l2m2m`, `vaapi`, `nvenc`, `qsv`, `rpi`, etc.) before falling back to highly optimized software encoders.
- **Custom Kernel Audio Module (DKMS):** Replaces fragile userspace audio routing with a dedicated "BerryAuto" virtual soundcard. Uses a zero-starvation `hrtimer` to guarantee a flawless, uninterrupted 48kHz audio stream to the car, while forcing PipeWire/PulseAudio to automatically resample desktop audio to meet strict Android Auto constraints natively.
- **Touch Injection:** Creates a virtual touchscreen via `uinput` to pass touch events to the local display server.

## Requirements

### Hardware

- A device with a USB Device Controller (UDC) capable of OTG (e.g., Raspberry Pi 4 via the USB-C port, Orange Pi, Rock Pi, etc.).

### OS Configuration

Enable the `dwc2` USB driver to allow the device to act as a USB gadget.  
_(On Raspberry Pi, add `dtoverlay=dwc2` to `/boot/firmware/config.txt` or `/boot/config.txt`)_

Add the required modules to `/etc/modules`:

```text
dwc2
libcomposite
uinput
evdi
snd-berryauto
```

Reboot your device after applying these changes.

### Dependencies

Install the required build tools, generic kernel headers for your OS, and graphics development libraries (Debian/Ubuntu):

```sh
sudo apt update
sudo apt install -y build-essential cmake pkg-config git \
    libssl-dev libprotobuf-dev protobuf-compiler \
    libavcodec-dev libavutil-dev libswscale-dev \
    linux-headers-$(uname -r) dkms libdrm-dev libasound2-dev
```

### 1. Compiling EVDI from Source

Because distribution package managers often contain outdated versions of EVDI, we will compile it directly from the official DisplayLink repository.

```sh
cd ~
git clone https://github.com/DisplayLink/evdi.git
cd evdi

# 1. Build and install the userspace library (libevdi)
cd library
make
sudo make install
sudo cp evdi_lib.h /usr/local/include/ # The EVDI Makefile forgets to do this!
sudo ldconfig # Refresh the system linker so BerryAuto can find the new library
cd ..

# 2. Build and install the kernel module via DKMS
# Extract the current version dynamically from the dkms.conf file
EVDI_VER=$(grep PACKAGE_VERSION module/dkms.conf | cut -d'=' -f2 | tr -d '"')

# Create the DKMS source directory and copy the module files into it
sudo mkdir -p /usr/src/evdi-$EVDI_VER
sudo cp -a module/. /usr/src/evdi-$EVDI_VER/

# Build and install the module
sudo dkms add -m evdi -v $EVDI_VER
sudo dkms build -m evdi -v $EVDI_VER
sudo dkms install -m evdi -v $EVDI_VER

# Load the newly installed module into the active kernel
sudo modprobe evdi
```

### 2. Building BerryAuto & Custom Audio Driver

Clone the repository and build the daemon:

```sh
cd ~
git clone https://github.com/Cxmrykk/BerryAuto.git
cd BerryAuto
```

First, compile and install the custom BerryAuto Audio DKMS kernel module. This guarantees drop-out free, zero-starvation audio streaming to the car:

```sh
sudo ./scripts/install_audio_dkms.sh
```

_(You can verify it installed correctly by running `aplay -l`. You should see `[BerryAutoAudio]` listed as a hardware device)._

Next, build the primary C++ daemon:

```sh
cd berryautod
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Installation & Setup

Helper scripts must be placed in your system path.

```sh
cd ~/BerryAuto
sudo ln -sf "$(pwd)/scripts/setup_opengal_gadget.sh" /usr/local/bin/
sudo chmod +x /usr/local/bin/setup_opengal_gadget.sh
```

Ensure your user is in the `video` and `audio` groups to access hardware encoders and ALSA devices:

```sh
sudo usermod -aG video,audio $(whoami)
```

Remove the password prompt from `sudo` so the runner script can configure the USB gadget without interrupting the connection process:

```sh
echo "$(whoami) ALL=(ALL) NOPASSWD:ALL" | sudo tee /etc/sudoers.d/$(whoami)
```

## Configuration

By default, BerryAuto works **out of the box** with zero configuration. It will automatically negotiate the best resolution with your phone, automatically search your system for hardware video encoders, and PipeWire/PulseAudio will automatically route and resample desktop audio to the new `BerryAutoAudio` virtual soundcard.

If you want to manually override the video encoder, adjust the bitrate, or force a specific resolution, you can create a configuration file at `~/.config/berryauto.conf` or `/etc/berryauto.conf`.

See [CONFIG.md](CONFIG.md) for a full list of available options and an example configuration.

## Running BerryAuto

Plug your phone into the OTG port, then execute:

```sh
cd ~/BerryAuto
./scripts/run_berryauto.sh
```

Because BerryAuto uses EVDI and a custom audio module, there are no screen-sharing prompts or permission pop-ups to click. The daemon will instantly create a virtual display, the Linux desktop will extend/resize to it, audio routing will latch into place, and streaming will begin.

## Autostart on Boot (Systemd Service)

To make BerryAuto run automatically on boot and restart seamlessly when you plug and unplug your phone, you should set it up as a systemd service.

From the project root directory (`~/BerryAuto`), run:

```sh
# Enable systemd linger for the current user so user-level systemd-run commands execute correctly
loginctl enable-linger $USER

# Create the service file
sudo tee /etc/systemd/system/berryauto.service > /dev/null <<EOF
[Unit]
Description=BerryAuto Daemon Runner
After=graphical.target systemd-modules-load.service
Wants=graphical.target

[Service]
Type=simple
User=$USER
Group=$(id -g -n)
WorkingDirectory=$PWD
ExecStart=$PWD/scripts/run_berryauto.sh
Restart=always
RestartSec=3

[Install]
WantedBy=graphical.target
EOF

# Reload systemd and enable the service
sudo systemctl daemon-reload
sudo systemctl enable --now berryauto.service
```

You can check the status of the daemon at any time using:

```sh
systemctl status berryauto.service
```
