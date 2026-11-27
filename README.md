# Paradox serial bridge (TTGO T-Display)

ESP32 firmware that connects to a Paradox MG/SP panel's serial port
(protocol: `SPXXXX-Serial-Port-Protocol.doc`, events: `Paradox.pdf` pp. 18-21).

## Manual

The bridge has a built-in user manual at `http://<bridge>/manual` (also the **Manual** button on
the main page and in Settings). It needs no login, so the recovery steps can be read even with a
forgotten password. The source is `src/manual_html.h`.

## Wiring

| Panel serial connector | T-Display |
|---|---|
| TX (5 V) | **through a divider** (1 kΩ series, 2 kΩ to GND) → GPIO27 (RX) |
| RX | GPIO26 (TX) — 3.3 V output, normally read fine by the 5 V input |
| GND | GND |

The ESP32 is not 5 V tolerant, so the divider on the panel TX line is required.
To power the board from the panel's aux supply, use a buck converter to 5 V.
Pins can be changed in `include/config.h`.

## Configure

Edit `include/config.h`: set the WiFi network, **change `WEB_PASSWORD`**, and
set `PARADOX_PC_PASSWORD` (panel section [911], default `0000`).

## Build / upload

```
python -m platformio run -t upload
python -m platformio device monitor
```

## Other ESP32 boards

The same firmware runs on any ESP32 dev board. Turn **Settings → Board → TTGO T-Display
screen** off, or set `DISPLAY_ENABLED 0` in `config.h` before the first upload. Then the
screen is never initialised and its pins (4, 5, 14, 16, 18, 19, 23, 34, 35) are free for the
panel UART. Everything else works the same through the web page and the USB console.

**Factory reset (any board):** hold the BOOT button (GPIO0) for 5 seconds while the bridge
is running. Saved settings are erased and it reboots with the `config.h` defaults.

## Use

- **Display**: right button (GPIO35) cycles pages: connection → zones → events.
  Left button (GPIO0) turns the backlight on or off.
- **Web UI**: `http://paradox.local/`, the station IP, or `http://192.168.4.1/`
  on the `ParadoxBridge` access point. Arm, disarm, bypass, PGM on/off, beep, and
  sync the panel clock.
- **JSON API**: `GET /api/status`, `POST /api/action` (`action`, `partition`, `zone`, `pgm`),
  `POST /api/time`. All endpoints use HTTP basic auth.
- **USB console** (115200): `help`, `status`, `zones`, `events`, `json`, `arm 1 stay`,
  `disarm 1`, `bypass 5`, `pgm 2 on`, `beep`, `time`, `raw on`.

## MQTT (Homebridge / Homey)

Enable it on **Settings → MQTT**: broker address, port, login and **base topic** (default
`paradox`). Every topic below starts with the base topic. States are retained and published
when they change; live panel events are pushed as they happen.

| Topic | Payload |
|---|---|
| `paradox/status` | `online` / `offline` (last will) |
| `paradox/area/1/state` | `disarmed` `stay` `sleep` `full` `alarm` |
| `paradox/area/1/homekit` | `D` `SA` `NA` `AA` `T` (HomeKit current state) |
| `paradox/area/1/homekit/target` | `D` `SA` `NA` `AA` |
| `paradox/area/1/json` | every area flag as JSON |
| `paradox/zone/3/open` | `true` / `false` (enabled zones only) |
| `paradox/zone/3/json` | name, open, alarm, bypassed, tamper, low battery, signal… |
| `paradox/panel/ac_power` | `true` (AC present) / `false` (AC failure) |
| `paradox/panel/battery_low` | `true` (battery low / missing) / `false` |
| `paradox/troubles`, `paradox/info`, `paradox/panel/link` | JSON / text |
| `paradox/event` | JSON per live event (not retained) |
| `paradox/action/result` | result of the last command |

Commands (if *Allow control over MQTT* is on):
`paradox/area/1/set` ← `full`/`away`/`AA`, `stay`/`home`/`SA`, `sleep`/`night`/`NA`, `disarm`/`off`/`D`;
`paradox/zone/3/bypass` ← anything (toggles); `paradox/pgm/2/set` ← `on`/`off`; `paradox/beep`.

**Homebridge** (`homebridge-mqttthing`). The HomeKit state codes match mqttthing's defaults:

```json
{
  "accessory": "mqttthing", "type": "securitySystem", "name": "Alarm",
  "url": "mqtt://BROKER_IP", "username": "...", "password": "...",
  "topics": {
    "getCurrentState": "paradox/area/1/homekit",
    "getTargetState":  "paradox/area/1/homekit/target",
    "setTargetState":  "paradox/area/1/set"
  }
},
{
  "accessory": "mqttthing", "type": "contactSensor", "name": "Front door",
  "url": "mqtt://BROKER_IP",
  "topics": { "getContactSensorState": "paradox/zone/1/open" },
  "onValue": "true", "offValue": "false"
}
```

For zones set to **Motion** on the Settings page, use `"type": "motionSensor"` with
`"getMotionDetected": "paradox/zone/N/open"` instead of the contact sensor.

**Home Assistant**: tick *Home Assistant discovery* (Settings → MQTT, prefix normally
`homeassistant`) and point Home Assistant's MQTT integration at the same broker. A
**Paradox alarm** device appears with:

- an *alarm panel* per area in use: disarmed / armed home (stay) / armed away (full) /
  armed night (sleep) / arming (exit delay) / pending (entry delay) / triggered.
- a *binary sensor* per enabled zone. Its device class comes from the zone's **Sensor** type
  on Settings → Zones (*Motion* → `motion`, *Contact* → `opening`). Bypass, tamper, alarm,
  battery and signal are available as attributes.
- diagnostics: panel connection, number of active troubles (the list is in the attributes).

Renaming, disabling or changing a zone's sensor type updates Home Assistant automatically.
A disabled zone's entity is removed. The alarm panel needs no code in Home Assistant, so
limit who can use the dashboard, or untick *Allow control over MQTT*.

**Homey (native app)**: see [`HomeyAddon/`](HomeyAddon/README.md). It's a Homey app with alarm-area
and zone devices and flow cards, talking to the bridge's HTTP API directly (no broker needed).

**Homey (via MQTT)**: install *MQTT Client* (and *MQTT Broker* if you don't have a broker). In Flows, use
"When a message is received on topic `paradox/area/1/state`" as a trigger, and "Publish
`full` to `paradox/area/1/set`" as an action.

## Apple HomeKit (built in)

Go to **Settings → Smart home → Connect with: Apple HomeKit**, then press Save (the bridge
reboots). HomeKit and MQTT are mutually exclusive, so switching to HomeKit turns MQTT off
(Home Assistant / Homebridge over MQTT stop). The Homey app keeps working either way.

The Smart home tab then shows the HomeKit status, the **setup QR code** and the **setup code**.
In the Home app tap **+ → Add Accessory** and scan the code, or tap *More options → Paradox
Alarm* and type the code. You get:

- one **Security System** per area in use: Home = Stay, Away = Full, Night = Sleep, Off = Disarm,
  Triggered = alarm.
- one **Contact** or **Motion** sensor per enabled zone (type from Settings → Zones), with
  Tampered and Low battery.
- **AC power** and **Panel battery** contact sensors: closed = OK, open = trouble (the battery
  sensor also sets Low battery). The area alarms show **Fault** while either has a trouble.

The accessory list is built at boot, so after changing zones, reboot the bridge.
Switching away from HomeKit deletes everything HomeSpan stored on the bridge (pairings, keys,
setup code, its copy of the WiFi login). Remove *Paradox Alarm* from the Home app too.
**New setup code** creates a new code. **Remove all pairings** removes the bridge from every
Home app. HomeKit uses port 1201 (the web page stays on 80) and the HomeSpan library.

**First install of 1.14.0 or later must be over USB:** HomeKit needs larger firmware slots
(`min_spiffs.csv`, 2 × 1.9 MB), and a partition change can't be done over the air. Settings are
kept, because their flash area doesn't move. OTA works again afterwards.

## Static IP

Go to **Settings → WiFi network → IP address**. **Automatic (DHCP)** is the default; **Static**
lets you enter the IP address, subnet mask, gateway and DNS servers. **Use current values**
fills them from the address the router gave the bridge. Reserve that address in your router,
or pick one outside its DHCP range. The fields are checked before saving (valid addresses,
gateway in the same subnet). If the bridge becomes unreachable after a mistake, hold BOOT for
5 s to reset the settings, or connect to its `ParadoxBridge` access point.

## Settings, firmware updates and backups

Settings live in the ESP32's flash. On the first boot every value is stored there, and
firmware updates (USB or OTA) never change them, even if `config.h` changes. To move them to
another board or keep a copy, use **Settings → Backup**:
- **Download backup** saves a JSON file with all settings plus zone names, modes and sensor types.
- **Restore backup** loads it and reboots.

The file contains your passwords, so keep it private.

## Firmware updates (OTA)

- **From the web page**: Settings → *Firmware update* → choose `.pio/build/ttgo-t-display/firmware.bin`
  → *Upload firmware*. The bridge installs it, reboots, and keeps all settings.
- **From PlatformIO over the network**: set `$env:PARADOX_OTA_PASSWORD` to the web password, then
  `python -m platformio run -e ota -t upload`. Can be turned off on the Settings page.
- The very first install of v1.5.0 or later has to go over USB.

## Panel configuration (read only)

After each login the bridge reads the panel's programming. It uses EEPROM *read*
commands (0x50) only. The driver refuses to send EEPROM write (0x60) or any command
other than 0x5F/0x00/0x30/0x40/0x50/0x70.

- Labels: zones 0x0010, PGMs 0x0210, areas 0x0310 (16 bytes each).
- Zone definitions: 0x0730, 3 bytes per zone (type, partition, options).

These addresses come from the open-source Paradox Alarm Interface; the protocol document
doesn't list them. The snapshot is cached in the board's flash, so zones are set up before
the panel answers. The cache is rewritten only when the panel's programming changes.

Zones the panel has disabled are hidden, and area 2 is hidden if no enabled zone uses it.
On **Settings → Zones** each zone can get a name and a mode, stored on the bridge only:
*Auto* follows the panel, *Always* shows it, *Never* hides it.
Console: `zone 3 enabled Kitchen door`, `zone 7 disabled`, `reread`.

## Notes

- The partition byte of a zone definition is read as a bitmask (1 = area 1, 2 = area 2,
  3 = both). The raw value is shown on the Settings page so this can be checked.
- "Sync panel clock" (0x30) is the only thing that changes anything on the panel besides
  arming/disarming/bypass/PGM actions. It sets the clock and never touches programming.
- AC/DC/battery levels are shown as the raw 0-255 values the protocol reports.
- `backup/original_flash_4MB.bin` is the board's firmware from before this project. To restore it:
  `python -m esptool --port COM3 write-flash 0 backup/original_flash_4MB.bin`
