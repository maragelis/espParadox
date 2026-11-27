#pragma once
// ============================================================================
//  Factory defaults - used only on a new board or after "Reset to defaults".
//  On first boot every value is stored in flash; from then on the saved values
//  are used and firmware updates (USB or OTA) never change them, even if this
//  file changes. Back them up / restore them on the Settings page.
//  Everything below except the baud rate and button pins can be changed at
//  runtime on the web Settings page (http://<bridge>/settings); saved values are
//  kept in flash and override these. To go back to these defaults: hold the
//  BOOT button (GPIO0) for 5 s while the bridge is running, use "Reset to
//  defaults" on the Settings page, or type "config reset" on the USB console.
// ============================================================================

// ---- WiFi ------------------------------------------------------------------
// Leave WIFI_SSID empty to run only as an access point.
// If the network can't be joined for 30 s, the access point starts as well,
// so the bridge stays reachable.
#define WIFI_SSID      ""
#define WIFI_PASSWORD  ""
#define AP_SSID        "ParadoxBridge"
#define AP_PASSWORD    "paradox123"      // at least 8 characters
#define HOSTNAME       "paradox"         // -> http://paradox.local

// IP address: 0 = DHCP (automatic), 1 = static (the values below)
#define WIFI_STATIC_IP   0
#define WIFI_IP          ""              // e.g. "192.168.2.114"
#define WIFI_SUBNET      "255.255.255.0"
#define WIFI_GATEWAY     ""              // e.g. "192.168.2.1"
#define WIFI_DNS1        ""              // empty = use the gateway
#define WIFI_DNS2        ""

// ---- Web UI / API login (HTTP basic auth) - CHANGE THESE -------------------
#define WEB_USER       "admin"
#define WEB_PASSWORD   "changeme"

// ---- Paradox panel serial port ---------------------------------------------
// The panel's serial connector is 5 V TTL. The ESP32 is NOT 5 V tolerant:
// put a voltage divider (e.g. 1k from panel TX, 2k to GND) on the RX line.
#define PARADOX_RX_PIN  27     // ESP32 RX  <- panel TX
#define PARADOX_TX_PIN  26     // ESP32 TX  -> panel RX
#define PARADOX_BAUD    9600   // fixed by the protocol (9600 8N1)

// PC password, panel section [911] (factory default 0000). Used to log in
// as a Winload-style connection (command 0x00 bytes 10-11).
#define PARADOX_PC_PASSWORD  "0000"

// Login method: 0 = PC password [911] (Winload-style), 1 = user code (NEware-style)
#define PARADOX_LOGIN_METHOD 0
#define PARADOX_USER_CODE    ""        // 4-6 digit keypad code, for login method 1

// Source ID and user ID (bytes 33-35 of every request) follow the login method:
// PC password -> 0x01 Winload direct, user 0; user code -> 0x05 NEware direct,
// user = the number the panel answers for the code.

// Read zone / PGM / partition labels from the panel EEPROM after login.
// The label addresses are NOT in the protocol document; they come from the
// open-source Paradox Alarm Interface project. Set to 0 to disable.
#define PARADOX_READ_LABELS  1

// ---- Time (used by "Sync panel clock") --------------------------------------
#define NTP_SERVER  "pool.ntp.org"
#define TZ_INFO     "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Greece / Eastern Europe

// ---- MQTT (Homebridge mqttthing, Homey MQTT Client, ...) --------------------
#define MQTT_ENABLED        0
#define MQTT_HOST           ""          // broker IP or hostname
#define MQTT_PORT           1883
#define MQTT_USER           ""
#define MQTT_PASSWORD       ""
#define MQTT_BASE_TOPIC     "paradox"
#define MQTT_ALLOW_CONTROL  1           // accept arm/disarm/bypass/PGM commands over MQTT
#define MQTT_RETAIN_ZONES   1           // retain zone/N/open + zone/N/json
#define MQTT_RETAIN_PANEL   1           // retain panel status (link, alarm, troubles, AC, battery, areas)
#define MQTT_HA_DISCOVERY   1           // publish Home Assistant MQTT discovery configs
#define MQTT_HA_PREFIX      "homeassistant"

// ---- BabyWare serial pass-through (switched on for a time window on Settings -> Panel) ----
#define PASSTHROUGH_PORT    10000
#define IP150_PASSWORD      "paradox"   // IP password BabyWare uses for its IP150 connection

// ---- Apple HomeKit (built in, HomeSpan) - only one of MQTT / HomeKit can be on ----
#define HOMEKIT_ENABLED     0

// ---- OTA firmware updates --------------------------------------------------
// Web upload (Settings page) is always available behind the web login.
// Network upload from PlatformIO (ArduinoOTA, password = web password):
#define OTA_NETWORK_ENABLED 1

// ---- Board -----------------------------------------------------------------
// 1 = TTGO T-Display (screen + buttons). 0 = any other ESP32 board: the screen
// is never initialised and its pins are left alone. Also changeable on the
// Settings page.
#ifndef DISPLAY_ENABLED   // the esp32-generic build sets it to 0 (platformio.ini)
#define DISPLAY_ENABLED   0
#endif

// T-Display buttons (only used when the display is enabled)
#define BUTTON_PAGE       35   // next display page
#define BUTTON_BACKLIGHT  0    // backlight on/off

// Factory reset: press this button within RESET_WINDOW_MS after power-on / restart and
// hold it for RESET_HOLD_MS. Pressing it later does nothing. GPIO0 is the BOOT button
// found on practically every ESP32 board (it can't be held while power is applied:
// the ESP32 would start in flashing mode).
#define BUTTON_RESET      0
#define RESET_WINDOW_MS   5000
#define RESET_HOLD_MS     10000
