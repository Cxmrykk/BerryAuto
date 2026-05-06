# BerryAuto

BerryAuto is a daemon that transforms a Linux single-board computer into a wired Android Auto receiver. It handles Android Auto Protocol (AAP) communication over USB OTG, captures the local display, encodes it to H.264/HEVC, and translates Android Auto touch inputs into local Linux virtual input devices.

While the Raspberry Pi 4 is the primary target, any Linux device with a USB Device Controller (UDC) and hardware video encoding capabilities (V4L2/OMX) is supported.

## Features

- **USB OTG Emulation:** Uses Linux FunctionFS to negotiate the Android Open Accessory (AOA) protocol.
- **Dynamic Screen Capture:** Automatically detects and utilizes Wayland (`wlr-screencopy`), PipeWire (GNOME/Mutter), or X11 (`XShm`).
- **Hardware Encoding:** Prioritizes V4L2/OMX hardware encoders (`h264_v4l2m2m`, `h264_omx`) before falling back to software (`libx264`).
- **Touch Injection:** Creates a virtual touchscreen via `uinput` to pass touch events to the local display server.
- **Dynamic Resolution:** Automatically negotiates and resizes the local desktop to match the vehicle/headunit specifications via `xrandr` or `wlr-randr`.

## Requirements

### Hardware

- A device with a USB Device Controller (UDC) capable of OTG (e.g., Raspberry Pi 4 via the USB-C port).
- Hardware video encoder (recommended).

### OS Configuration (Raspberry Pi 4)

You must enable the `dwc2` USB driver to allow the Pi to act as a USB device.
Add the following to `/boot/config.txt` (or `/boot/firmware/config.txt`):

```ini
dtoverlay=dwc2
```

Add the following to `/etc/modules`:

```text
dwc2
libcomposite
uinput
```

Reboot your device after applying these changes.

### Dependencies

Install the required build tools and libraries (Debian/Ubuntu):

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config \
    libssl-dev libprotobuf-dev protobuf-compiler \
    libavcodec-dev libavutil-dev libswscale-dev \
    libx11-dev libxext-dev libwayland-dev wayland-protocols \
    libpipewire-0.3-dev libglib2.0-dev wlr-randr \
```

## Building

Clone the repository and build the daemon:

```sh
cd ~
git clone https://github.com/Cxmrykk/BerryAuto.git
cd BerryAuto/berryautod
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Installation & Setup

The daemon relies on helper scripts to configure the USB gadget and handle display resizing. These must be placed in your system path.

```sh
# From the project root
cd ~/BerryAuto
sudo ln -sf "$(pwd)/scripts/setup_opengal_gadget.sh" /usr/local/bin/
sudo ln -sf "$(pwd)/scripts/resize_desktop.sh" /usr/local/bin/
```

Make sure that the helper scripts are executable:

```sh
sudo chmod +x /usr/local/bin/setup_opengal_gadget.sh
sudo chmod +x /usr/local/bin/resize_desktop.sh
```

Ensure your user is in the `video` group to access hardware encoders:

```sh
sudo usermod -aG video $(whoami)
```

## Running BerryAuto

Do not run the daemon directly. Use the provided runner script which sets up the Wayland/X11 environment variables, configures FunctionFS, and handles the accessory morphing sequence.

First you need to remove the password prompt from sudo. Run the following first:

```sh
echo "$(whoami) ALL=(ALL) NOPASSWD:ALL" | sudo tee /etc/sudoers.d/$(whoami)
```

Plug your phone into the OTG port, then execute:

```sh
./scripts/run_berryauto.sh
```

The script will:

1. Initialize the USB gadget as an MTP device.
2. Wait for the Android device to connect.
3. Perform the AOA handshake to switch the phone into Accessory Mode.
4. Restart the daemon to handle the active Android Auto video and control streams.
