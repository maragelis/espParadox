#include "settings.h"

#include <Preferences.h>

#include "config.h"

Settings cfg;

namespace {
constexpr const char* kNs = "bridge";
// Usable on any ESP32 board. Excluded: 0/1/3 (boot button, USB serial),
// 6-11 (flash), 12 (strapping: idle-high panel TX would break boot).
constexpr uint8_t kOutputPins[] = {2, 13, 15, 17, 21, 22, 25, 26, 27, 32, 33};
constexpr uint8_t kInputOnlyPins[] = {36, 39};
// Used by the T-Display (screen, backlight, ADC, buttons) - free when the display is off.
constexpr uint8_t kDisplayOutputPins[] = {4, 5, 14, 16, 18, 19, 23};
constexpr uint8_t kDisplayInputPins[] = {34, 35};

template <size_t N>
bool inList(const uint8_t (&list)[N], int pin) {
  for (uint8_t p : list)
    if (p == pin) return true;
  return false;
}
}  // namespace

bool settingsPinReservedByDisplay(int pin) {
  return inList(kDisplayOutputPins, pin) || inList(kDisplayInputPins, pin);
}

bool settingsTxPinValid(int pin, bool displayEnabled) {
  return inList(kOutputPins, pin) || (!displayEnabled && inList(kDisplayOutputPins, pin));
}

bool settingsRxPinValid(int pin, bool displayEnabled) {
  return settingsTxPinValid(pin, displayEnabled) || inList(kInputOnlyPins, pin) ||
         (!displayEnabled && inList(kDisplayInputPins, pin));
}

void settingsLoad() {
  Preferences p;
  p.begin(kNs, true);  // read-only; keys that are missing return the default
  cfg.wifiSsid = p.getString("wifiSsid", WIFI_SSID);
  cfg.wifiPassword = p.getString("wifiPass", WIFI_PASSWORD);
  cfg.apSsid = p.getString("apSsid", AP_SSID);
  cfg.apPassword = p.getString("apPass", AP_PASSWORD);
  cfg.hostname = p.getString("hostname", HOSTNAME);
  cfg.webUser = p.getString("webUser", WEB_USER);
  cfg.webPassword = p.getString("webPass", WEB_PASSWORD);
  cfg.rxPin = p.getUChar("rxPin", PARADOX_RX_PIN);
  cfg.txPin = p.getUChar("txPin", PARADOX_TX_PIN);
  cfg.pcPassword = p.getString("pcPass", PARADOX_PC_PASSWORD);
  cfg.sourceId = p.getUChar("sourceId", PARADOX_SOURCE_ID);
  cfg.userId = p.getUShort("userId", PARADOX_USER_ID);
  cfg.readLabels = p.getBool("labels", PARADOX_READ_LABELS);
  cfg.ntpServer = p.getString("ntp", NTP_SERVER);
  cfg.tz = p.getString("tz", TZ_INFO);
  cfg.displayEnabled = p.getBool("display", DISPLAY_ENABLED);
  cfg.mqttEnabled = p.getBool("mqttOn", MQTT_ENABLED);
  cfg.mqttHost = p.getString("mqttHost", MQTT_HOST);
  cfg.mqttPort = p.getUShort("mqttPort", MQTT_PORT);
  cfg.mqttUser = p.getString("mqttUser", MQTT_USER);
  cfg.mqttPassword = p.getString("mqttPass", MQTT_PASSWORD);
  cfg.mqttBase = p.getString("mqttBase", MQTT_BASE_TOPIC);
  cfg.mqttAllowControl = p.getBool("mqttCtl", MQTT_ALLOW_CONTROL);
  cfg.haDiscovery = p.getBool("haDisc", MQTT_HA_DISCOVERY);
  cfg.haPrefix = p.getString("haPrefix", MQTT_HA_PREFIX);
  cfg.otaNetwork = p.getBool("otaNet", OTA_NETWORK_ENABLED);
  p.end();

  // Guard against a bad saved pin locking up the UART.
  if (!settingsRxPinValid(cfg.rxPin, cfg.displayEnabled)) cfg.rxPin = PARADOX_RX_PIN;
  if (!settingsTxPinValid(cfg.txPin, cfg.displayEnabled) || cfg.txPin == cfg.rxPin)
    cfg.txPin = PARADOX_TX_PIN;
}

void settingsSave() {
  Preferences p;
  p.begin(kNs, false);
  p.putString("wifiSsid", cfg.wifiSsid);
  p.putString("wifiPass", cfg.wifiPassword);
  p.putString("apSsid", cfg.apSsid);
  p.putString("apPass", cfg.apPassword);
  p.putString("hostname", cfg.hostname);
  p.putString("webUser", cfg.webUser);
  p.putString("webPass", cfg.webPassword);
  p.putUChar("rxPin", cfg.rxPin);
  p.putUChar("txPin", cfg.txPin);
  p.putString("pcPass", cfg.pcPassword);
  p.putUChar("sourceId", cfg.sourceId);
  p.putUShort("userId", cfg.userId);
  p.putBool("labels", cfg.readLabels);
  p.putString("ntp", cfg.ntpServer);
  p.putString("tz", cfg.tz);
  p.putBool("display", cfg.displayEnabled);
  p.putBool("mqttOn", cfg.mqttEnabled);
  p.putString("mqttHost", cfg.mqttHost);
  p.putUShort("mqttPort", cfg.mqttPort);
  p.putString("mqttUser", cfg.mqttUser);
  p.putString("mqttPass", cfg.mqttPassword);
  p.putString("mqttBase", cfg.mqttBase);
  p.putBool("mqttCtl", cfg.mqttAllowControl);
  p.putBool("haDisc", cfg.haDiscovery);
  p.putString("haPrefix", cfg.haPrefix);
  p.putBool("otaNet", cfg.otaNetwork);
  p.end();
}

void settingsReset() {
  Preferences p;
  p.begin(kNs, false);
  p.clear();
  p.end();
}
