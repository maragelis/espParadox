#include "settings.h"

#include <IPAddress.h>
#include <Preferences.h>

#include "config.h"

Settings cfg;

namespace {
constexpr const char* kNs = "bridge";
// Every NVS key settingsSave() writes (used to detect a partial set).
constexpr const char* kKeys[] = {"wifiSsid", "wifiPass", "apSsid",   "apPass",   "hostname", "webUser",  "webPass",
                                 "rxPin",    "txPin",    "pcPass",   "labels",   "ntp",
                                 "tz",       "display",  "mqttOn",   "mqttHost", "mqttPort", "mqttUser", "mqttPass",
                                 "mqttBase", "mqttCtl",  "haDisc",   "haPrefix", "otaNet",   "mqttRetZ", "mqttRetP", "staticIp", "ip",       "subnet",   "gateway",  "dns1",     "dns2",     "loginMeth", "userCode", "hkOn",     "hkCode"};
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
  cfg.staticIp = p.getBool("staticIp", WIFI_STATIC_IP);
  cfg.ip = p.getString("ip", WIFI_IP);
  cfg.subnet = p.getString("subnet", WIFI_SUBNET);
  cfg.gateway = p.getString("gateway", WIFI_GATEWAY);
  cfg.dns1 = p.getString("dns1", WIFI_DNS1);
  cfg.dns2 = p.getString("dns2", WIFI_DNS2);
  cfg.webUser = p.getString("webUser", WEB_USER);
  cfg.webPassword = p.getString("webPass", WEB_PASSWORD);
  cfg.rxPin = p.getUChar("rxPin", PARADOX_RX_PIN);
  cfg.txPin = p.getUChar("txPin", PARADOX_TX_PIN);
  cfg.pcPassword = p.getString("pcPass", PARADOX_PC_PASSWORD);
  cfg.loginMethod = p.getUChar("loginMeth", PARADOX_LOGIN_METHOD) == 1 ? 1 : 0;
  cfg.userCode = p.getString("userCode", PARADOX_USER_CODE);
  cfg.readLabels = p.getBool("labels", PARADOX_READ_LABELS);
  cfg.ntpServer = p.getString("ntp", NTP_SERVER);
  cfg.tz = p.getString("tz", TZ_INFO);
  cfg.displayEnabled = p.getBool("display", DISPLAY_ENABLED);
  cfg.mqttEnabled = p.getBool("mqttOn", MQTT_ENABLED);
  cfg.homekitEnabled = p.getBool("hkOn", HOMEKIT_ENABLED);
  cfg.homekitCode = p.getString("hkCode", "");
  cfg.mqttHost = p.getString("mqttHost", MQTT_HOST);
  cfg.mqttPort = p.getUShort("mqttPort", MQTT_PORT);
  cfg.mqttUser = p.getString("mqttUser", MQTT_USER);
  cfg.mqttPassword = p.getString("mqttPass", MQTT_PASSWORD);
  cfg.mqttBase = p.getString("mqttBase", MQTT_BASE_TOPIC);
  cfg.mqttAllowControl = p.getBool("mqttCtl", MQTT_ALLOW_CONTROL);
  cfg.mqttRetainZones = p.getBool("mqttRetZ", MQTT_RETAIN_ZONES);
  cfg.mqttRetainPanel = p.getBool("mqttRetP", MQTT_RETAIN_PANEL);
  cfg.haDiscovery = p.getBool("haDisc", MQTT_HA_DISCOVERY);
  cfg.haPrefix = p.getString("haPrefix", MQTT_HA_PREFIX);
  cfg.otaNetwork = p.getBool("otaNet", OTA_NETWORK_ENABLED);
  bool complete = true;
  for (const char* k : kKeys)
    if (!p.isKey(k)) complete = false;
  p.end();

  // Guard against a bad saved pin locking up the UART.
  if (!settingsRxPinValid(cfg.rxPin, cfg.displayEnabled)) cfg.rxPin = PARADOX_RX_PIN;
  if (!settingsTxPinValid(cfg.txPin, cfg.displayEnabled) || cfg.txPin == cfg.rxPin)
    cfg.txPin = PARADOX_TX_PIN;

  // First boot (or first boot of a firmware that added settings): store every
  // value now, so a later firmware with different config.h defaults can't change it.
  if (!complete) {
    settingsSave();
    Serial.println("[settings] stored the full settings set in flash");
  }
}

void settingsSave() {
  Preferences p;
  p.begin(kNs, false);
  p.putString("wifiSsid", cfg.wifiSsid);
  p.putString("wifiPass", cfg.wifiPassword);
  p.putString("apSsid", cfg.apSsid);
  p.putString("apPass", cfg.apPassword);
  p.putString("hostname", cfg.hostname);
  p.putBool("staticIp", cfg.staticIp);
  p.putString("ip", cfg.ip);
  p.putString("subnet", cfg.subnet);
  p.putString("gateway", cfg.gateway);
  p.putString("dns1", cfg.dns1);
  p.putString("dns2", cfg.dns2);
  p.putString("webUser", cfg.webUser);
  p.putString("webPass", cfg.webPassword);
  p.putUChar("rxPin", cfg.rxPin);
  p.putUChar("txPin", cfg.txPin);
  p.putString("pcPass", cfg.pcPassword);
  p.putUChar("loginMeth", cfg.loginMethod);
  p.putString("userCode", cfg.userCode);
  p.remove("sourceId");  // no longer settings (follow the login method)
  p.remove("userId");
  p.putBool("labels", cfg.readLabels);
  p.putString("ntp", cfg.ntpServer);
  p.putString("tz", cfg.tz);
  p.putBool("display", cfg.displayEnabled);
  p.putBool("mqttOn", cfg.mqttEnabled);
  p.putBool("hkOn", cfg.homekitEnabled);
  p.putString("hkCode", cfg.homekitCode);
  p.putString("mqttHost", cfg.mqttHost);
  p.putUShort("mqttPort", cfg.mqttPort);
  p.putString("mqttUser", cfg.mqttUser);
  p.putString("mqttPass", cfg.mqttPassword);
  p.putString("mqttBase", cfg.mqttBase);
  p.putBool("mqttCtl", cfg.mqttAllowControl);
  p.putBool("mqttRetZ", cfg.mqttRetainZones);
  p.putBool("mqttRetP", cfg.mqttRetainPanel);
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

// ---- validation -------------------------------------------------------------

namespace {
bool isHex4(const String& s) {
  if (s.length() != 4) return false;
  for (char c : s)
    if (!isxdigit((unsigned char)c)) return false;
  return true;
}

bool isHostname(const String& s) {
  if (s.isEmpty() || s.length() > 32) return false;
  for (char c : s)
    if (!(isalnum((unsigned char)c) || c == '-')) return false;
  return true;
}

bool parseIp(const String& s, IPAddress& out) { return s.length() && out.fromString(s); }

// 255.255.255.0 style: ones followed by zeros, not all zeros.
bool isMask(const IPAddress& m) {
  uint32_t v = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
  return v != 0 && ((~v + 1) & ~v) == 0;
}

uint32_t ipValue(const IPAddress& a) {
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3];
}

bool isTopic(const String& s, size_t maxLen) {
  return !s.isEmpty() && s.length() <= maxLen && s.indexOf('+') < 0 && s.indexOf('#') < 0;
}
}  // namespace

const char* settingsValidate(const Settings& n) {
  if (n.wifiSsid.length() > 32) return "WiFi name is longer than 32 characters";
  if (n.wifiPassword.length() && (n.wifiPassword.length() < 8 || n.wifiPassword.length() > 63))
    return "WiFi password must be 8-63 characters";
  if (n.apSsid.isEmpty() || n.apSsid.length() > 32) return "Access point name must be 1-32 characters";
  if (n.apPassword.length() < 8 || n.apPassword.length() > 63) return "Access point password must be 8-63 characters";
  if (!isHostname(n.hostname)) return "Hostname: letters, digits and '-' only (max 32)";
  if (n.staticIp) {
    IPAddress ip, mask, gw, d;
    if (!parseIp(n.ip, ip)) return "Static IP: enter a valid IP address, e.g. 192.168.1.50";
    if (!parseIp(n.subnet, mask) || !isMask(mask)) return "Static IP: enter a valid subnet mask, e.g. 255.255.255.0";
    if (!parseIp(n.gateway, gw)) return "Static IP: enter a valid gateway (your router), e.g. 192.168.1.1";
    uint32_t m = ipValue(mask);
    if ((ipValue(ip) & m) != (ipValue(gw) & m)) return "Static IP: the IP address and the gateway must be in the same subnet";
    if ((ipValue(ip) & ~m) == 0 || (ipValue(ip) & ~m) == ~m) return "Static IP: that address is the subnet's network / broadcast address";
    if (ipValue(ip) == ipValue(gw)) return "Static IP: the IP address can't be the same as the gateway";
    if (n.dns1.length() && !parseIp(n.dns1, d)) return "Static IP: DNS server 1 isn't a valid IP address";
    if (n.dns2.length() && !parseIp(n.dns2, d)) return "Static IP: DNS server 2 isn't a valid IP address";
  }
  if (n.webUser.isEmpty()) return "Web user can't be empty";
  if (n.webPassword.length() < 4) return "Web password must be at least 4 characters";
  if (!settingsRxPinValid(n.rxPin, n.displayEnabled))
    return n.displayEnabled && settingsPinReservedByDisplay(n.rxPin)
               ? "RX pin is used by the display - pick another or turn the display off"
               : "Invalid RX pin";
  if (!settingsTxPinValid(n.txPin, n.displayEnabled))
    return n.displayEnabled && settingsPinReservedByDisplay(n.txPin)
               ? "TX pin is used by the display - pick another or turn the display off"
               : "Invalid TX pin";
  if (n.rxPin == n.txPin) return "RX and TX must be different pins";
  if (n.loginMethod == 0 && !isHex4(n.pcPassword)) return "PC password must be exactly 4 digits (0-9, A-F)";
  if (n.loginMethod == 1) {
    if (n.userCode.length() < 4 || n.userCode.length() > 6) return "User code must be 4 to 6 digits";
    for (char c : n.userCode)
      if (c < '0' || c > '9') return "User code: digits only";
  }
  if (n.loginMethod > 1) return "Unknown login method";
  if (n.ntpServer.isEmpty() || n.ntpServer.length() > 64) return "NTP server must be 1-64 characters";
  if (n.tz.isEmpty() || n.tz.length() > 64) return "Time zone must be 1-64 characters";
  if (n.mqttEnabled && n.homekitEnabled) return "Choose MQTT or HomeKit - not both";
  if (n.homekitEnabled && n.wifiSsid.isEmpty()) return "HomeKit needs your WiFi network (Network tab)";
  if (n.mqttEnabled && n.mqttHost.isEmpty()) return "MQTT broker address is required";
  if (n.mqttHost.length() > 64) return "MQTT broker address is too long";
  if (n.mqttPort < 1) return "MQTT port must be 1-65535";
  if (!isTopic(n.mqttBase, 48)) return "MQTT base topic: 1-48 characters, no + or #";
  if (!isTopic(n.haPrefix, 32)) return "Home Assistant discovery prefix: 1-32 characters, no + or #";
  return nullptr;
}

// ---- backup / restore --------------------------------------------------------

void settingsToJson(JsonObject o) {
  o["wifiSsid"] = cfg.wifiSsid;
  o["wifiPassword"] = cfg.wifiPassword;
  o["apSsid"] = cfg.apSsid;
  o["apPassword"] = cfg.apPassword;
  o["hostname"] = cfg.hostname;
  o["staticIp"] = cfg.staticIp;
  o["ip"] = cfg.ip;
  o["subnet"] = cfg.subnet;
  o["gateway"] = cfg.gateway;
  o["dns1"] = cfg.dns1;
  o["dns2"] = cfg.dns2;
  o["webUser"] = cfg.webUser;
  o["webPassword"] = cfg.webPassword;
  o["rxPin"] = cfg.rxPin;
  o["txPin"] = cfg.txPin;
  o["pcPassword"] = cfg.pcPassword;
  o["loginMethod"] = cfg.loginMethod;
  o["userCode"] = cfg.userCode;
  o["readLabels"] = cfg.readLabels;
  o["ntpServer"] = cfg.ntpServer;
  o["tz"] = cfg.tz;
  o["displayEnabled"] = cfg.displayEnabled;
  o["mqttEnabled"] = cfg.mqttEnabled;
  o["homekitEnabled"] = cfg.homekitEnabled;
  o["homekitCode"] = cfg.homekitCode;
  o["mqttHost"] = cfg.mqttHost;
  o["mqttPort"] = cfg.mqttPort;
  o["mqttUser"] = cfg.mqttUser;
  o["mqttPassword"] = cfg.mqttPassword;
  o["mqttBase"] = cfg.mqttBase;
  o["mqttAllowControl"] = cfg.mqttAllowControl;
  o["mqttRetainZones"] = cfg.mqttRetainZones;
  o["mqttRetainPanel"] = cfg.mqttRetainPanel;
  o["haDiscovery"] = cfg.haDiscovery;
  o["haPrefix"] = cfg.haPrefix;
  o["otaNetwork"] = cfg.otaNetwork;
}

void settingsFromJson(JsonObjectConst o, Settings& s) {
  auto str = [&](const char* k, String& dst) {
    if (o[k].is<const char*>()) dst = o[k].as<const char*>();
  };
  auto boolean = [&](const char* k, bool& dst) {
    if (o[k].is<bool>()) dst = o[k].as<bool>();
  };
  auto u8 = [&](const char* k, uint8_t& dst) {
    if (o[k].is<int>()) dst = (uint8_t)constrain(o[k].as<int>(), 0, 255);
  };
  str("wifiSsid", s.wifiSsid);
  str("wifiPassword", s.wifiPassword);
  str("apSsid", s.apSsid);
  str("apPassword", s.apPassword);
  str("hostname", s.hostname);
  boolean("staticIp", s.staticIp);
  str("ip", s.ip);
  str("subnet", s.subnet);
  str("gateway", s.gateway);
  str("dns1", s.dns1);
  str("dns2", s.dns2);
  str("webUser", s.webUser);
  str("webPassword", s.webPassword);
  u8("rxPin", s.rxPin);
  u8("txPin", s.txPin);
  str("pcPassword", s.pcPassword);
  u8("loginMethod", s.loginMethod);
  str("userCode", s.userCode);
  // "sourceId" / "userId" in older backups are ignored (they follow the login method now).
  boolean("readLabels", s.readLabels);
  str("ntpServer", s.ntpServer);
  str("tz", s.tz);
  boolean("displayEnabled", s.displayEnabled);
  boolean("mqttEnabled", s.mqttEnabled);
  boolean("homekitEnabled", s.homekitEnabled);
  str("homekitCode", s.homekitCode);
  str("mqttHost", s.mqttHost);
  if (o["mqttPort"].is<long>()) s.mqttPort = (uint16_t)constrain(o["mqttPort"].as<long>(), 0L, 65535L);
  str("mqttUser", s.mqttUser);
  str("mqttPassword", s.mqttPassword);
  str("mqttBase", s.mqttBase);
  boolean("mqttAllowControl", s.mqttAllowControl);
  boolean("mqttRetainZones", s.mqttRetainZones);
  boolean("mqttRetainPanel", s.mqttRetainPanel);
  boolean("haDiscovery", s.haDiscovery);
  str("haPrefix", s.haPrefix);
  boolean("otaNetwork", s.otaNetwork);
}