# GNOME Wayland Setup Guide for BerryAuto

Running BerryAuto under a GNOME Wayland (Mutter) desktop environment requires extra configuration. Unlike wlroots-based compositors (like Wayfire), GNOME employs a strict security model that intentionally blocks headless screen recording and custom resolution injection.

To make BerryAuto work seamlessly on GNOME, you must compile a native Rust utility to handle screen resizing and authorize the screen-sharing portal on your first run.

## 1. Install Build Dependencies

The standard `rustc` compiler provided by Debian/Ubuntu package managers is often too outdated to compile modern Rust tools. You must install the official Rust toolchain via `rustup`, along with the required DBus libraries.

```sh
# Install DBus development headers and build tools
sudo apt update
sudo apt install -y git build-essential pkg-config libdbus-1-dev

# Install the latest Rust toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Load the Rust environment into your current terminal session
source "$HOME/.cargo/env"
```

## 2. Compile `gnome-randr-rust`

Because GNOME's Mutter compositor does not support standard `wlr-randr` or `xrandr` commands under Wayland, BerryAuto uses `gnome-randr-rust` to interface with Mutter's private DBus Display Configurator.

Clone the repository, compile it for maximum performance (`--release`), and install it globally:

```sh
# Clone the repository
cd ~
git clone https://github.com/maxwellainatchi/gnome-randr-rust.git # Or use a maintained fork
cd gnome-randr-rust

# Build the release binary
cargo build --release

# Install it globally so BerryAuto scripts can find it
sudo ln -sf "$(pwd)/target/release/gnome-randr" /usr/local/bin/
sudo chmod +x /usr/local/bin/gnome-randr
```

Verify the installation by listing your current monitors and supported resolutions:

```sh
gnome-randr query
```

## 3. The Screen-Cast Portal "Share" Prompt (First Run Only)

GNOME uses the XDG Desktop Portal for screen capture (`org.freedesktop.portal.ScreenCast`). By design, this requires interactive user consent.

**On your very first run:**

1. Connect a physical monitor and mouse to your Raspberry Pi.
2. Make sure your Pi is connected to a head unit and run `./scripts/run_berryauto.sh`
3. A GNOME pop-up will appear on your screen asking: _"Share your screen? opengal_emitter wants to share..."_.
4. **You must click "Share".**

**Future Runs:**
Once you click "Share" the first time, GNOME generates a "Restore Token". BerryAuto automatically captures this token and saves it to `~/.config/berryauto_portal_token.txt`. On all subsequent runs, BerryAuto injects this token to completely bypass the prompt, allowing it to start headlessly.

## 4. Force HDMI Output in the Kernel

When the Raspberry Pi runs headless, we won't likely have a HDMI output connected (since we are using the head unit as the output). Therefore we need to tell the linux kernel to "pretend" that a monitor is connected with our preferred head unit resolution.

1. Open `/boot/firmware/cmdline.txt`

```sh
sudo nano /boot/firmware/cmdline.txt
```

2. Add the following text to the end of the existing line (do not create a new line, everything in this file must stay on one single line):

```sh
# Change 800x480 to your head unit screen resolution
# We absolutely need `e` and not `d`, otherwise pipewire doesn't send frames
video=HDMI-A-1:800x480@60e
```
