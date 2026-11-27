# How to install

This guide takes you from an empty ESP32 board to a working bridge between your Paradox
**Spectra SP** or **Magellan MG** alarm panel and your network. After that you can control the
panel from the web page, Home Assistant (MQTT), Apple HomeKit or Homey.

**Contents**
1. [What you need](#1-what-you-need)
2. [Install the tools](#2-install-the-tools)
3. [Get the code](#3-get-the-code)
4. [Upload the firmware](#4-upload-the-firmware)
5. [First set-up: WiFi and password](#5-first-set-up-wifi-and-password)
6. [Connect the panel](#6-connect-the-panel)
7. [Log in to the panel](#7-log-in-to-the-panel)
8. [Connect your smart home](#8-connect-your-smart-home)
9. [Updating later](#9-updating-later)
10. [Troubleshooting](#10-troubleshooting)

> **Safety first.** The bridge can arm and disarm your alarm. Change the web password during
> set-up (step 5) and keep the bridge on your home network only. **Never forward its ports to
> the internet.**
>
> The bridge only *reads* the panel's programming and never changes it. Arming, disarming,
> bypassing and PGMs use the panel's normal commands.

---

## 1. What you need

| Item | Notes |
|---|---|
| **ESP32 board** with 4 MB flash | A **LilyGO TTGO T-Display** shows status on its screen. Any other ESP32 dev board works too, without the screen. |
| USB cable | Data cable, not charge-only. |
| Paradox **SP** or **MG** panel | Uses the panel's 4-pin **serial** connector. |
| 2 resistors: **1 kΩ** and **2 kΩ** | Voltage divider for the panel's 5 V TX line. **Required**: the ESP32 is not 5 V tolerant. |
| Wires | 3 wires to the panel: TX, RX, GND. |
| Power | USB power supply, **or** a small buck converter (panel AUX 12–14 V → 5 V) to power it from the panel. |
| A computer | Windows, macOS or Linux, for the first upload. |

You also need the panel's **PC password** (section **[911]**, factory `0000`) **or** a keypad
**user code**; see [step 7](#7-log-in-to-the-panel).

## 2. Install the tools

The firmware is built with **PlatformIO**. Pick one of these:

- **VS Code (easiest):** install [Visual Studio Code](https://code.visualstudio.com/), then the
  **PlatformIO IDE** extension from the Extensions panel.
- **Command line:** install [Python 3](https://www.python.org/downloads/), then run:
  ```
  pip install platformio
  ```

On Windows, if the board doesn't show up as a COM port, install the USB driver for its chip:
[CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) or
[CH340/CH9102](https://www.wch-ic.com/downloads/CH341SER_EXE.html). Which one depends on the
board; the chip name is printed next to the USB socket.

## 3. Get the code

```
git clone https://github.com/maragelis/espParadox.git
cd espParadox
```

(Or use **Code → Download ZIP** on GitHub and unpack it.)

You don't have to edit anything: WiFi and all other settings are made later on the bridge's web
page. If you want different factory defaults, for example your WiFi so the bridge joins it
straight away, edit [`include/config.h`](include/config.h) before uploading.

## 4. Upload the firmware

Plug the board into the computer. Then choose the build for your board:

| Board | Build name |
|---|---|
| LilyGO TTGO T-Display | `ttgo-t-display` |
| Any other ESP32 | `esp32-generic` (screen off) |

**Find the board's port:**
- Windows: `COM3`, `COM4` … (Device Manager → Ports)
- macOS: `/dev/cu.usbserial-…`
- Linux: `/dev/ttyUSB0`

PlatformIO can list them: `python -m platformio device list`.

**Upload** (replace the build name and port with yours):

```
python -m platformio run -e ttgo-t-display -t upload --upload-port COM3
```

In VS Code you can instead pick the environment in the PlatformIO toolbar and press **Upload**.
Then set `upload_port` in [`platformio.ini`](platformio.ini) to your port.

The first build downloads the ESP32 tools and libraries, which takes a few minutes.

**If the upload fails** with *"Failed to connect"* or *"Wrong boot mode"*:
- Hold the board's **BOOT** button, tap **EN/RST**, release BOOT, and upload again.
- Slow the upload down. Add `--upload-speed 115200`, or set `upload_speed = 115200` in
  `platformio.ini`.

**Check it starts** (optional): `python -m platformio device monitor --port COM3`. You should
see `=== Paradox serial bridge v… ===`.

## 5. First set-up: WiFi and password

1. On a phone or laptop, join the bridge's own WiFi: **`ParadoxBridge`**, password **`paradox123`**.
2. Open **http://192.168.4.1**. Log in with user **`admin`** and password **`changeme`**.
3. Open **Settings → Network**. Press **Scan**, choose your WiFi, enter its password and press **Save**.
4. The bridge reboots and joins your network. Find its new address:
   - **T-Display:** the screen shows it.
   - **Any board:** try **http://paradox-&lt;last number of its IP&gt;.local**, e.g.
     `paradox-84.local` at 192.168.2.84. Or look in your router's device list.
5. **Change the web password:** *Settings → Security*. The page warns you until you do.
6. **Recommended:** give the bridge a fixed address. Either reserve one in your router (DHCP
   reservation), or use *Settings → Network → Static IP*. Its name and MQTT topics include the
   IP number, so they then never change.

If the bridge can't join your WiFi within 30 seconds, the `ParadoxBridge` network comes back so
you can fix the settings.

## 6. Connect the panel

**Power the panel down first** (AC and battery) before you connect anything.

| Panel serial connector | ESP32 |
|---|---|
| **TX** (5 V) | **1 kΩ** in series → **GPIO27** (RX). Also **2 kΩ** from GPIO27 to **GND**. |
| **RX** | **GPIO26** (TX) directly. The panel reads 3.3 V fine. |
| **GND** | **GND** |

```
panel TX ──[ 1 kΩ ]──┬── GPIO27 (ESP32 RX)
                     │
                  [ 2 kΩ ]
                     │
panel GND ───────────┴── GND
panel RX  ────────────── GPIO26 (ESP32 TX)
```

- **Pin order:** the connector's pins are marked on the panel board. Check them in your panel's
  installation manual.
- **Powering the bridge from the panel:** feed the buck converter from the panel's **AUX +/–**,
  set it to 5 V, and connect it to the board's **5V/VIN** and **GND**. Don't power it from the
  panel and USB at the same time.
- **Other pins:** you can choose different RX/TX pins under *Settings → System*.

## 7. Log in to the panel

Open **Settings → Panel** and choose a **login method**:

| Method | What you need | Notes |
|---|---|---|
| **PC password** | Panel section **[911]**, factory `0000` | Full access, like BabyWare/Winload. The settings page shows how to read [911] from a keypad. |
| **User code** | A keypad user code | Works when you don't know [911]. Access is limited to that user's areas, and events show that user. |

Save, then go back to the main page. **Panel online** should appear, and the bridge reads the
zone and area names from the panel. In **Settings → Zones** you can rename zones, hide zones and
choose contact or motion sensor. These settings live on the bridge only; the panel isn't changed.

## 8. Connect your smart home

Use **one** of MQTT or HomeKit at a time (*Settings → Smart home*). The Homey app works with
either.

- **Home Assistant / Homebridge (MQTT):** enter your broker under *Settings → Smart home → MQTT*
  and turn on **Home Assistant discovery**. Areas, zones, AC power and battery then appear in
  Home Assistant by themselves. The topics are listed in the [README](README.md#mqtt-homebridge--homey).
- **Apple HomeKit:** choose HomeKit and reboot. In the Home app, choose **Add Accessory** and
  scan the QR code. It's shown on the settings page, and on the T-Display screen (fourth page).
- **Homey:** install the **Paradox SP/MG Bridge** app. In the app settings enter the bridge's web
  login; the bridge is found automatically. Then add an *Alarm area* device and your zones. To
  install the app from source, see [`HomeyAddon/README.md`](HomeyAddon/README.md).
- **BabyWare:** *Settings → Panel → BabyWare pass-through* lets BabyWare reach the panel through
  the bridge, using the IP150 / Static IP connection type. Everything else pauses while it's on,
  and a reboot returns to normal.

## 9. Updating later

You only need USB once. After that, update over WiFi:

1. Build the new version:
   ```
   git pull
   python -m platformio run -e ttgo-t-display
   ```
2. Upload it on the bridge's web page: **Settings → System → Firmware update**. Choose
   `.pio/build/<build name>/firmware.bin` (e.g. `.pio/build/ttgo-t-display/firmware.bin`).

**Your settings are kept** through updates. Still, make a backup first: *Settings → System →
Backup* downloads all settings as a file, and *Restore* loads them back.

## 10. Troubleshooting

| Problem | What to do |
|---|---|
| Can't find the bridge on the network | Use the T-Display screen or your router's device list. Or wait 30 s for the `ParadoxBridge` WiFi to come back and use http://192.168.4.1. |
| Forgot the web password | Factory reset: power the bridge on, then **within 5 s press BOOT and hold it for 10 s**. Don't hold BOOT *while* plugging in. All settings return to the defaults, so restore your backup afterwards. |
| *No response from panel* | Check the wiring: TX and RX may be swapped, the divider may be missing, or there's no common GND. Check the RX/TX pins in *Settings → System*. |
| *Invalid PC password* / *invalid user code* | Check [911] or the user code, or try the other login method. |
| *Winload on phone line* / session busy | Another PC session (BabyWare, IP module) is still open. Wait a minute; the bridge retries by itself. |
| Zones show wrong or no names | Press **Re-read panel configuration** under *Settings → Zones*. |
| Upload over USB fails | See [step 4](#4-upload-the-firmware): use the BOOT button or a lower upload speed. |

The bridge's built-in **manual** at `http://<bridge>/manual` has more detail. It needs no login.

---

*This is an unofficial community project. It is not made, supported or endorsed by Paradox
Security Systems.*
