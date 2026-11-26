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
#define FW_VERSION "1.8.0"
