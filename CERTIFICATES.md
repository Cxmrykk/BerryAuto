# Extracting Official Android Auto Certificates via Live Memory Scraping

**Objective:** Extract the official Android Auto Private Key and Leaf Certificate from a running Android environment. We will use a rooted Raspberry Pi 4, Frida, and a raw memory scanner to catch the keys in RAM during a live TLS handshake with the official Desktop Head Unit. Feel free to use the latest versions of the files described below.

---

## Part 1: Device Preparation (Raspberry Pi 4)

### Phase 1: Prerequisites

- **Hardware:** Raspberry Pi 4 (2GB RAM minimum), high-speed microSD card, an HDMI monitor, and a **USB Flash Drive (FAT32/exFAT)**. Also a USB mouse and keyboard.
- **Files to Download:**
  1.  **ROM:** `lineage-23.2-20260128-UNOFFICIAL-KonstaKANG-rpi4.zip` (Extract the `.img` file to your PC).
  2.  **Magisk Script:** `KonstaKANG-rpi-magisk-v30.7.zip`.
  3.  **Magisk App:** `Magisk-v30.7.apk`.
  4.  **GApps:** `MindTheGapps-16.0.0-arm64-xxxxxxxx.zip`.
  5.  **Resize Script:** `KonstaKANG-rpi-resize.zip` (to use your full SD card capacity).
  6.  **AppManager & Android Auto:** Download the AppManager APK (via F-Droid/GitHub) and the Android Auto `.xapk` (via APKPure).
- **USB Prep:** Copy files 2 through 6 onto your USB Flash Drive.

### Phase 2: Base Installation

1.  **Flash the Image:** Use [Raspberry Pi Imager](https://www.raspberry-pi.com/software/) to flash the LineageOS `.img` file to your microSD card.
2.  **First Boot:** Insert the microSD card into the Pi and power it on.
3.  **Initial Setup:** Complete the Android setup wizard and connect to the same WiFi network as your PC. You will need a USB mouse and keyboard.

### Phase 3: Root & Google Apps (TWRP via USB Drive)

1.  **Enable Advanced Reboot:** Go to **Settings** -> **System** -> **Buttons** -> **Power menu**. Enable **Advanced restart**.
2.  **Boot to TWRP:** Hold the Power button _(Holding F5 on your keyboard also works)_ -> **Restart** -> **Recovery**.
3.  **Mount the USB Drive:**
    - Plug your USB drive into the Raspberry Pi.
    - In the TWRP main menu, tap **Mount** and check **USB-OTG** (or USB Storage). Go back to the main menu.
4.  **Flash GApps & Factory Reset (Crucial):**
    - Tap **Install** -> tap **Select Storage** -> choose **USB-OTG**.
    - Select the `MindTheGapps` zip and swipe to flash.
    - **IMPORTANT:** After flashing GApps, you **must** go to **Wipe** -> **Swipe to Factory Reset**. If you skip this, Google apps will crash constantly.
5.  **Flash Magisk & Resize:**
    - Go back to **Install** and flash `KonstaKANG-rpi-magisk-v30.7.zip`.
    - Go to **Install** and flash `KonstaKANG-rpi-resize.zip`.
6.  **Reboot:** Select **Reboot** -> **System**.

### Phase 4: Finalizing Root & Apps

1.  Once booted back into Android, plug in your USB drive. Open the default **Files** app, navigate to your USB drive, and install **Magisk-v30.7.apk**.

    > Alternatively, there might already be a magisk placeholder installed in your app drawer, in which case you open and follow the steps.

2.  Open Magisk, allow "Additional Setup," and let the device reboot.
3.  **Enable ADB Root:** Go to **Settings** -> **System** -> **Developer options** -> enable **Rooted debugging** and **USB debugging**.
4.  **Enable Network ADB:** Go to **Settings** -> **System** -> **Raspberry Pi settings** -> **Remote access** -> enable **ADB** and note the IP address. _(used in phase 6)_
5.  **Install Android Auto:** Because the Pi is an unsupported device, the Play Store will block Android Auto. Use the Files app to install the **AppManager** APK from your USB drive. Open AppManager, grant it Root access, and use it to install your Android Auto **`.xapk`** file.

---

## Part 2: The Extraction Environment

### Phase 5: Setting up the Desktop Head Unit (DHU) on PC

To trigger the handshake, your PC needs to act like a car.

1.  Open **Android Studio**.
2.  Go to **Tools** -> **SDK Manager**.
3.  Select the **SDK Tools** tab.
4.  Check **Android Auto Desktop Head Unit emulator** and click Apply to install.
5.  On Linux, the DHU binary will be located at `~/Android/Sdk/extras/google/auto/`.
    _(You will run `./desktop-head-unit --usb` from this directory later)._

### Phase 6: Setting up Frida Server (Over WiFi)

Because plugging the Pi into the PC triggers a USB mode switch (which kills ADB over USB), we must perform the memory injection over WiFi.

1.  On your PC, install Frida: `pip install frida-tools`. If you are in an externally managed environment, use a virtual environment:
    ```bash
    python -m venv venv
    source venv/bin/activate
    pip install frida-tools
    ```
2.  Download `frida-server-XX.X.X-android-arm64.xz` from the [Frida releases page](https://github.com/frida/frida/releases) and extract the binary.
3.  Push it to the Pi and start it as a network service:

    ```bash
    # Connect via WiFi (Ensure both devices are on the same network)
    adb connect <PI_IP_ADDRESS>:5555

    adb push frida-server-*-android-arm64 /data/local/tmp/frida-server
    adb shell "chmod 755 /data/local/tmp/frida-server"

    # CRITICAL: Disable SELinux, otherwise memory scanning fails
    adb shell "su -c 'setenforce 0'"

    # Kill any hung instances of frida-server just in case
    adb shell "su -c 'pkill -9 frida-server'"

    # Start Frida listening on all network interfaces in the background
    adb shell "su -c '/data/local/tmp/frida-server -l 0.0.0.0 &'"
    ```

---

## Part 3: The Python Scraper & Triggering

### Phase 7: The Scraper Script

On your PC, save the following script as `scrapper.py`.
_Make sure to change `PI_IP` to your Raspberry Pi's actual IP address._

```python
import frida
import time
import threading
import sys
import re
import hashlib

TARGET_STRINGS = ["gearhead:car"]
PI_IP = "192.168.X.XXX" # <--- CHANGE THIS TO YOUR PI'S IP

seen_pids = set()
seen_hashes = set()

PRIV_KEY_REGEX = re.compile(r"(-----BEGIN (?:RSA )?PRIVATE KEY-----.*?-----END (?:RSA )?PRIVATE KEY-----)", re.DOTALL)
CERT_REGEX = re.compile(r"(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)", re.DOTALL)

def process_memory_dump(raw_bytes, proc_name, address):
    text = raw_bytes.decode('utf-8', errors='ignore')

    for key in PRIV_KEY_REGEX.findall(text):
        key_hash = hashlib.md5(key.encode()).hexdigest()
        if key_hash not in seen_hashes:
            seen_hashes.add(key_hash)
            print(f"\n========================================================")
            print(f" 🚨 NEW PRIVATE KEY EXTRACTED 🚨")
            print(f" Process: {proc_name} | Addr: {address}")
            filename = f"android_auto_extracted_{key_hash[:6]}.key"
            with open(filename, "w") as f: f.write(key + "\n")
            print(f"[+] Saved cleanly to: {filename}")

    for cert in CERT_REGEX.findall(text):
        cert_hash = hashlib.md5(cert.encode()).hexdigest()
        if cert_hash not in seen_hashes:
            seen_hashes.add(cert_hash)
            print(f"\n--------------------------------------------------------")
            print(f" 📜 NEW CERTIFICATE EXTRACTED")
            print(f" Process: {proc_name} | Addr: {address}")
            filename = f"android_auto_extracted_{cert_hash[:6]}.crt"
            with open(filename, "w") as f: f.write(cert + "\n")
            print(f"[+] Saved cleanly to: {filename}")

def on_message(message, data):
    if message['type'] == 'send' and data:
        process_memory_dump(data, message['payload']['proc'], message['payload']['addr'])

def attach_and_scan(device, pid, name):
    try:
        session = device.attach(pid)
        js_code = """
        var processName = "REPLACE_NAME";
        console.log("[+] Stealth Scanner injected into " + processName);
        function scanRAM() {
            Process.enumerateRanges('rw-').forEach(function (range) {
                try {
                    Memory.scan(range.base, range.size, "2d 2d 2d 2d 2d 42 45 47 49 4e", {
                        onMatch: function (addr) { send({proc: processName, addr: addr}, addr.readByteArray(4096)); }
                    });
                } catch(e) {}
            });
        }
        scanRAM();
        setInterval(scanRAM, 2000);
        """.replace("REPLACE_NAME", name)

        script = session.create_script(js_code)
        script.on('message', on_message)
        script.load()
    except Exception: pass

def main():
    print(f"[*] Connecting to {PI_IP}...")
    device = frida.get_device_manager().add_remote_device(f"{PI_IP}:27042")
    print("[*] Connected! Waiting for 'gearhead:car' to spawn...")
    print("[*] (Plug in your USB / start DHU now. Console will stay silent until a key is found.)")

    while True:
        try:
            for proc in device.enumerate_processes():
                if proc.pid not in seen_pids and any(t in proc.name for t in TARGET_STRINGS):
                    seen_pids.add(proc.pid)
                    t = threading.Thread(target=attach_and_scan, args=(device, proc.pid, proc.name))
                    t.daemon = True
                    t.start()
        except frida.ServerNotRunningError: break
        except Exception: pass
        time.sleep(1)

if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: print("\n[*] Exiting cleanly...")
```

### Phase 8: Triggering the Handshake

1.  Run the Python script on your PC: `python3 scrapper.py`. Make sure the enter the virtual environment first if you initialized it before.
2.  Open a second terminal, navigate to the DHU folder (`~/Android/Sdk/extras/google/auto/`), and prepare the emulator: `./desktop-head-unit --usb`.
3.  Connect the Raspberry Pi to your PC via a USB data cable.
4.  Android Auto will launch in the background on the Pi, spawning `gearhead:car`.
5.  The Python script will detect it, inject the memory scanner, and wait for the TLS handshake.
6.  **Boom!** The console will light up, and the raw `.key` and `.crt` files will drop into your folder.

---

## Part 4: Verification & Integration

Because the TLS handshake includes the full certificate chain, you will likely get 1 Private Key and 3-4 Certificates. You must find out which `.crt` matches your `.key`.

1.  **Get the Modulus Hash of the Private Key:**
    ```bash
    openssl rsa -noout -modulus -in android_auto_extracted_*.key | openssl md5
    ```
2.  **Find the matching Certificate:**
    Run this loop to check the modulus hash of all dumped certificates:
    ```bash
    for cert in *.crt; do echo "$cert:"; openssl x509 -noout -modulus -in "$cert" | openssl md5; done
    ```
    _The certificate that produces the exact same MD5 hash as your private key is your Leaf Certificate._
3.  **Integration:**
    Rename the matching pair to `android_auto.key` and `android_auto.crt`.

Congratulations, you now have all the ingredients to connect to a real head unit!
