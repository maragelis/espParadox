Paradox SP/MG Bridge connects your Paradox Magellan (MG) or Spectra SP alarm panel to Homey through a small ESP32 bridge that plugs into the panel's serial port.

Arm, disarm and watch your alarm from Homey:
- Alarm area devices with Homey's alarm control (armed, partially armed, disarmed), the panel's own Stay, Sleep and Full modes, alarm and ready-to-arm status.
- Zone devices as door/window or motion sensors, with tamper, low battery and bypass status.
- Flows: react to any panel event, a mode change or an alarm; arm or disarm, bypass a zone, switch a PGM output or make the panel beep.
- Timeline: every arm, disarm and alarm, with who did it (the panel user's name, keyswitch, auto-arm or Homey) and which zone set off an alarm.

How it works: the app talks to the bridge over your local network (its web API), so everything stays in your home and no cloud or MQTT broker is needed. Status updates every 2 seconds.

Instructions: the app settings page has a How to section (getting started, adding areas and zones, arming, flow ideas, timeline, troubleshooting).

Setup: open the app settings (Apps > Paradox SP/MG Bridge > Configure) and enter the bridge's web login. The bridge is found automatically on your network; you can also type its IP address. Then add an "Alarm area" device and the zones you want. Zone names, which zones are shown and their sensor type (contact or motion) are set on the bridge's Settings page.

The panel's own programming is never changed. Arming and disarming use the panel's normal commands.

Requires the ESP32 Paradox serial bridge (espParadox), firmware 1.6 or later (1.6.9 or later for automatic discovery; 1.20 or later recommended).

This is an unofficial, community app. It is not made, supported or endorsed by Paradox Security Systems.
