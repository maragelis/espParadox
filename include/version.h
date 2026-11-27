#pragma once
// Firmware version - bump on every change.
//   1.0.0  Paradox serial driver, display, web UI / API, USB console
//   1.1.0  Runtime settings page (WiFi, login, panel serial, time) stored in flash
//   1.2.0  Read-only panel configuration read + cache, local zone names / enable
//   1.3.0  Version number on the web pages, display, API and console
//   1.4.0  Display on/off setting for other ESP32 boards; BOOT-button (5 s) factory reset
//   1.5.0  MQTT (Homebridge mqttthing / Homey), configurable base topic,
//          OTA: firmware upload on the Settings page + PlatformIO network upload
//   1.6.0  Home Assistant MQTT discovery; per-zone sensor type (contact / motion)
//   1.6.1  Reboot button in the top bar of both web pages
//   1.6.2  HA discovery: publish area 1 + diagnostics before the panel answers;
//          entities unavailable while the panel link is down
//   1.6.3  Settings button to force HA discovery publish; WiFi join retries + disconnect reason log
//   1.6.4  HA discovery: publish enabled zones without waiting for the panel
//   1.6.5  Disconnect / connect panel button (web top bar, console 'panel ...')
//   1.6.6  Zone definitions only used for product IDs in the protocol doc (0x1A reads garbage)
//   1.6.7  Frame counters reset every 10 minutes
//   1.6.8  PGM outputs card collapsible (remembered per browser)
//   1.6.9  mDNS service _paradox._tcp (TXT id, version, api) for Homey discovery
//   1.7.0  Read user labels (read-only); events name the user ("Armed by Maria"), JSON 'user'
//   1.7.1  Retry config EEPROM reads (lost answers) instead of skipping the block
//   1.8.0  AC failure: event on change, acFailure in status, MQTT panel/ac_power, HA sensor
//   1.8.1  Low battery: event on change, batteryLow in status, MQTT panel/battery_low, HA sensor
//   1.9.0  Settings pinned in flash (firmware updates never change them), backup / restore,
//          PC password [911] how-to, user ID as plain number
//   1.10.0 MQTT settings: retain zone states / retain panel status (each on/off)
//   1.11.0 WiFi IP address: DHCP or static (IP, mask, gateway, DNS) in Settings
//   1.12.0 Change password section (current / new / confirm, no reboot); factory-password warning
//   1.12.1 Settings page split into tabs (Network, Panel, Zones, MQTT, Security, System)
//   1.12.2 Settings: one global Save (settings + zones + password), RX/TX pins and Time on System tab
//   1.13.0 Login method: PC password [911] or user code (NEware-style), comparison table in Settings
//   1.13.1 User-code login tries both code layouts once; stops retrying after refusals (Connect resumes)
//   1.13.2 Source ID / user ID removed from settings - derived from the login method
//   1.13.3 Login refusals show the panel's error code
//   1.13.4 Login: 0x13 (old session open) -> close it and retry; invalid user code -> other code layout once
//   1.14.0 Apple HomeKit (HomeSpan): areas as security systems, zones as contact/motion sensors;
//          Smart home = none / MQTT / HomeKit (one at a time); 1.9 MB app partitions (USB once)
//   1.14.1 HomeKit off: HomeSpan data (pairings, keys, setup code, WiFi copy) removed at boot
//   1.15.0 Built-in user manual at /manual (no login needed), linked from the page headers
//   1.15.1 HomeKit: AC power + panel battery sensors, Fault on the area alarms
//   1.15.2 Free RAM badge at the top of the main page; memory / settings storage / firmware details
//   1.15.3 MQTT: find + delete old Paradox Home Assistant discovery entries (keeps this bridge's)
//   1.16.0 BabyWare serial pass-through (TCP, RFC 2217 or raw), switched on for a time window
//   1.16.1 Pass-through: drain the old session's last panel answer before handing over
//   1.17.0 BabyWare mode: everything except the web page stops; only a reboot restores normal operation
//   1.17.1 Pass-through diagnostics: first bytes of each direction shown in status / Panel tab
//   1.18.0 IP150 emulation: BabyWare's "IP150 / Static IP" connection works directly (AES-256, PAI protocol)
#define FW_VERSION "1.18.0"
