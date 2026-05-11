# BerryAuto Configuration

By default, BerryAuto requires **no configuration**. It will automatically negotiate the best video stream with your device and dynamically scan your system's FFmpeg libraries for hardware video encoders (`v4l2m2m`, `vaapi`, `nvenc`, etc.), falling back to software encoding only if necessary.

However, if you are running BerryAuto on a niche SBC, have specialized hardware, or want to tweak the video quality, you can create a configuration file.

## Configuration File Locations

BerryAuto will look for a configuration file in the following order:

1. `~/.config/berryauto.conf` _(Recommended for standard users)_
2. `/etc/berryauto.conf` _(Recommended for system-wide setups)_
3. `./berryauto.conf` _(In the current working directory where the daemon was executed)_

## Available Options

The configuration file uses a simple `key=value` format. Lines starting with `#` are ignored as comments.

### Video Encoding

- **`video_encoder`**  
  Forces the daemon to use a specific FFmpeg encoder. If left blank, BerryAuto will auto-discover the best available hardware encoder.  
  _Examples:_ `h264_v4l2m2m`, `h264_vaapi`, `h264_nvenc`, `h264_omx`, `libx264`

- **`disable_hw_encoding`**  
  Setting this to `true` or `1` will force BerryAuto to ignore all hardware encoders and rely entirely on software encoding (e.g., `libx264`).  
  _Default:_ `false`

- **`video_bitrate`**  
  Overrides the dynamic bitrate calculation with a fixed target (in bits per second).  
  _Example:_ `8000000` (8 Mbps)

- **`video_profile`**  
  Sets the H.264/HEVC profile. Some older car head units or specific hardware decoders require a strict profile.  
  _Examples:_ `baseline`, `main`, `high`

- **`video_preset`**  
  Sets the encoder preset (primarily affects software encoders like `libx264`). Slower presets yield better quality at the cost of CPU usage.  
  _Default:_ `ultrafast`  
  _Examples:_ `superfast`, `veryfast`, `faster`, `fast`, `medium`

- **`video_tune`**  
  Sets the encoder tuning (primarily affects software encoders).  
  _Default:_ `zerolatency`

### Display Overrides

_Note: Overriding display settings will force the EVDI virtual monitor to a specific size, but Android Auto will still stretch/scale to fit whatever the car head unit originally requested. Use these primarily for debugging._

- **`force_width`**  
  Forces the internal video pipeline to a specific width in pixels.  
  _Example:_ `1920`

- **`force_height`**  
  Forces the internal video pipeline to a specific height in pixels.  
  _Example:_ `1080`

- **`force_fps`**  
  Forces the encoder to target a specific frame rate (e.g., `30` or `60`).

---

## Example Configuration (`~/.config/berryauto.conf`)

```ini
# BerryAuto Configuration File
# Save to ~/.config/berryauto.conf

# --------------------------
# Video Encoding Settings
# --------------------------

# Force a specific hardware encoder (Uncomment to use)
# video_encoder=h264_vaapi

# Force software encoding only
# disable_hw_encoding=false

# Override dynamic bitrate to a static 10 Mbps
# video_bitrate=10000000

# Set a specific H.264 profile
# video_profile=main

# Software encoding tuning parameters
# video_preset=ultrafast
# video_tune=zerolatency

# --------------------------
# Display Override Settings
# --------------------------

# Force 720p at 30 FPS regardless of head unit negotiation
# force_width=1280
# force_height=720
# force_fps=30
```
