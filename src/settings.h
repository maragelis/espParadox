#pragma once
// Runtime settings, stored in NVS flash. Defaults come from include/config.h
// and are used until something is saved from the web Settings page.

#include <Arduino.h>

struct Settings {
  // WiFi
  String wifiSsid, wifiPassword;
  String apSsid, apPassword;
  String hostname;
  // IP address: DHCP or static
  bool staticIp;
  String ip, subnet, gateway, dns1, dns2;
  // Web login
  String webUser, webPassword;
  // Paradox serial
  uint8_t rxPin, txPin;
  String pcPassword;  // 4 hex digits, section [911]
  uint8_t sourceId;
  uint16_t userId;
  bool readLabels;
  // Time
  String ntpServer, tz;
  // Board
  bool displayEnabled;  // TTGO T-Display screen + buttons; off for other ESP32 boards
  // MQTT
  bool mqttEnabled;
  String mqttHost;
  uint16_t mqttPort;
  String mqttUser, mqttPassword;
  String mqttBase;        // topic prefix, e.g. "paradox"
  bool mqttAllowControl;  // accept commands over MQTT
  bool mqttRetainZones;   // retain zone/N/open + zone/N/json
  bool mqttRetainPanel;   // retain panel status + area topics
  bool haDiscovery;       // Home Assistant MQTT discovery
  String haPrefix;        // discovery prefix, normally "homeassistant"
  // OTA
  bool otaNetwork;        // ArduinoOTA (PlatformIO network upload)
};

extern Settings cfg;

// Load from NVS. Settings missing from NVS take the config.h defaults and are then
// saved, so later firmware (with other defaults) never changes an existing setup.
void settingsLoad();
void settingsSave();   // write cfg to NVS
void settingsReset();  // erase NVS settings (defaults apply after reboot)

// Pins that can be used for the panel UART. With the display enabled the
// T-Display's screen/button pins are excluded. Input-only pins: RX only.
bool settingsRxPinValid(int pin, bool displayEnabled);
bool settingsTxPinValid(int pin, bool displayEnabled);
bool settingsPinReservedByDisplay(int pin);

// Checks a complete settings set. Returns nullptr if valid, else a message.
const char* settingsValidate(const Settings& s);

// Backup / restore as JSON (includes passwords - keep backups private).
// settingsFromJson only changes fields present in `in`.
#include <ArduinoJson.h>
void settingsToJson(JsonObject out);
void settingsFromJson(JsonObjectConst in, Settings& s);
