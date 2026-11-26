# Paradox SP/MG Bridge — Homey app

Homey app (Apps SDK v3) for the ESP32 Paradox serial bridge in this repository. It talks to
the bridge's HTTP API directly over your network: status every 2 seconds from `/api/status`,
commands to `/api/action`. No MQTT broker is needed.

## Devices

**Alarm area** (one per area in use on the bridge)
- Homey's standard home-alarm control: *armed* = full arm, *partially armed* = stay or sleep
  (choose in the device settings), *disarmed*.
- *Alarm mode* picker with the panel's own modes: disarmed / stay / sleep / full (shows *in alarm*).
- *Alarm* (`alarm_generic`) and *Ready to arm*.

**Alarm zone** (one per zone enabled on the bridge)
- Contact or motion sensor, following the zone's **Sensor** type on the bridge's
  Settings → Zones page. It switches automatically if you change it there.
- Tamper, low battery, bypassed.

Devices show as unavailable when the bridge can't be reached, the panel link is down, or a
zone is disabled on the bridge.

## Flow cards

| Type | Card |
|---|---|
| Trigger (app) | A Paradox panel event was received (tokens: event text, group, sub-group, area, panel time) |
| Trigger (area) | Alarm mode changed (token: mode) — plus Homey's standard alarm / home-alarm triggers |
| Condition (area) | Alarm mode is … · Area is ready to arm |
| Condition (zone) | Zone is bypassed — plus the standard contact / motion / tamper conditions |
| Action (area) | Set alarm mode to … · Make the panel beep · Switch PGM *n* on/off |
| Action (zone) | Toggle zone bypass |

## Install on your Homey Pro

1. Install [Node.js](https://nodejs.org) (LTS), then the Homey CLI: `npm install -g homey`
2. `homey login`
3. From this folder:
   - `homey app run` runs the app on your Homey from your PC (logs in the terminal; it stops
     when you close it), or
   - `homey app install` installs it permanently.
4. **Apps → Paradox SP/MG Bridge → Configure**: enter the bridge's web user and password. Leave the address
   empty: the bridge is found automatically via mDNS (`_paradox._tcp`, firmware 1.6.9+). The page lists
   the bridges it found; **Use this address** pins one. **Test connection** checks without saving.
5. **Devices → + → Paradox SP/MG Bridge → Alarm area** (then **Alarm zone**). If the app settings already
   reach the bridge, pairing goes straight to the list; otherwise it asks once and saves to the app settings.

`app.json` is generated from `.homeycompose/` and `drivers/*/driver*.compose.json` by the Homey CLI.
Edit those files, not `app.json`.

Publishing to the Homey App Store would additionally need PNG images (`assets/images/small.png`
250×175, `large.png` 500×350, `xlarge.png` 1000×700, plus per driver) and an author e-mail in
`.homeycompose/app.json`. `homey app validate --level publish` lists what's missing.

## Notes

- With the address left empty, a changed bridge IP is picked up automatically (mDNS). A self-hosted
  Homey in Docker needs host networking to see mDNS; otherwise type the IP (and reserve it on your router).
- Changing the bridge's web password? Update it once in the app settings.
- Arming / disarming goes through the panel's normal commands; nothing in the panel's
  programming is changed.
