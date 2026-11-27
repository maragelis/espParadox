// Paradox MG/SP serial bridge for the TTGO T-Display.
//
//  - Talks to the panel's serial port (UART2) using the SPXXXX serial protocol
//  - Shows WiFi / panel connection info, zones and events on the built-in display
//  - Web UI + JSON API for status and control (see web.h)
//  - USB serial console (115200 baud, type "help")

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "events.h"
#include "homekit.h"
#include "passthrough.h"
#include "mqtt.h"
#include "ota.h"
#include "net.h"
#include "paradox.h"
#include "settings.h"
#include "version.h"
#include "zones.h"
#include "web.h"

static paradox::Panel panel;

// ---------------------------------------------------------------------------
// WiFi: station mode if configured, with an access point fallback.

static bool apActive = false;
static uint32_t lastStaOkMs = 0;
static constexpr uint32_t kApFallbackMs = 30000;

static bool staConfigured() { return cfg.wifiSsid.length() > 0; }

// Static IP (Settings -> WiFi): must be set before WiFi.begin(). DHCP otherwise.
static void applyStaticIp() {
  if (!cfg.staticIp) return;
  IPAddress ip, mask, gw, d1, d2;
  if (!ip.fromString(cfg.ip) || !mask.fromString(cfg.subnet) || !gw.fromString(cfg.gateway)) {
    Serial.println("[wifi] static IP settings invalid - using DHCP");
    return;
  }
  if (!d1.fromString(cfg.dns1)) d1 = gw;  // no DNS given: the router usually answers
  if (!d2.fromString(cfg.dns2)) d2 = IPAddress(0, 0, 0, 0);
  if (WiFi.config(ip, gw, mask, d1, d2))
    Serial.printf("[wifi] static IP %s, gateway %s, mask %s\n", cfg.ip.c_str(), cfg.gateway.c_str(), cfg.subnet.c_str());
  else
    Serial.println("[wifi] could not set the static IP - using DHCP");
}

static void startAp() {
  WiFi.mode(staConfigured() ? WIFI_AP_STA : WIFI_AP);
  // An AP password shorter than 8 characters is invalid; fall back to an open AP.
  const char* pass = cfg.apPassword.length() >= 8 ? cfg.apPassword.c_str() : nullptr;
  WiFi.softAP(cfg.apSsid.c_str(), pass);
  apActive = true;
  Serial.printf("[wifi] access point '%s' at %s\n", cfg.apSsid.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void netBegin() {
  WiFi.setHostname(cfg.hostname.c_str());
  if (staConfigured()) {
    // Log why the station drops / fails to join (reason codes: esp_wifi_types.h,
    // e.g. 2 auth expired, 15 wrong password / handshake timeout, 201 network not found).
    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
          static uint32_t lastLog = 0;
          if (millis() - lastLog > 5000) {
            lastLog = millis();
            Serial.printf("[wifi] disconnected, reason %u\n", info.wifi_sta_disconnected.reason);
          }
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // more reliable joins / lower latency
    applyStaticIp();
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    Serial.printf("[wifi] joining '%s'\n", cfg.wifiSsid.c_str());
  } else {
    startAp();
  }
  lastStaOkMs = millis();
  configTzTime(cfg.tz.c_str(), cfg.ntpServer.c_str());
  if (MDNS.begin(cfg.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    // Dedicated service so apps (Homey, ...) can find the bridge: _paradox._tcp
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    MDNS.addService("paradox", "tcp", 80);
    MDNS.addServiceTxt("paradox", "tcp", "id", mac);
    MDNS.addServiceTxt("paradox", "tcp", "version", FW_VERSION);
    MDNS.addServiceTxt("paradox", "tcp", "api", "/api/status");
  }
}

void netLoop() {
  if (!staConfigured()) return;
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    lastStaOkMs = millis();
    if (!wasConnected) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    if (apActive) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apActive = false;
    }
  } else {
    // Auto-reconnect doesn't retry after a failed first join, so retry ourselves -
    // unless HomeKit is on: HomeSpan then does the reconnects (two would collide).
    static uint32_t lastBeginMs = 0;
    if (!cfg.homekitEnabled && millis() - lastBeginMs > 15000 && millis() - lastStaOkMs > 15000) {
      lastBeginMs = millis();
      Serial.printf("[wifi] retrying '%s'\n", cfg.wifiSsid.c_str());
      WiFi.disconnect(false, false);
      applyStaticIp();
      WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    }
    if (!apActive && millis() - lastStaOkMs > kApFallbackMs) {
      Serial.println("[wifi] can't join network, starting access point (still retrying)");
      startAp();
    }
  }
  wasConnected = connected;
}

NetInfo netInfo() {
  NetInfo n;
  n.staConfigured = staConfigured();
  n.staConnected = WiFi.status() == WL_CONNECTED;
  n.apActive = apActive;
  n.ssid = n.staConnected ? WiFi.SSID() : String();
  n.ip = n.staConnected ? WiFi.localIP().toString() : String();
  n.rssi = n.staConnected ? WiFi.RSSI() : 0;
  n.gateway = n.staConnected ? WiFi.gatewayIP().toString() : String();
  n.subnet = n.staConnected ? WiFi.subnetMask().toString() : String();
  n.dns1 = n.staConnected ? WiFi.dnsIP(0).toString() : String();
  n.dns2 = n.staConnected ? WiFi.dnsIP(1).toString() : String();
  n.apSsid = cfg.apSsid;
  n.apPassword = cfg.apPassword.length() >= 8 ? cfg.apPassword : String("(open)");
  n.wantedSsid = cfg.wifiSsid;
  n.apIp = apActive ? WiFi.softAPIP().toString() : String();
  n.timeValid = time(nullptr) > 1700000000;
  return n;
}

// ---------------------------------------------------------------------------
// USB serial console

static void printHelp() {
  Serial.println(
      "Paradox bridge v" FW_VERSION "\nCommands:\n"
      "  status                  panel / link / partition summary\n"
      "  zones                   all zones\n"
      "  events                  recent events\n"
      "  json                    full status JSON\n"
      "  arm <1|2> <full|stay|sleep>\n"
      "  disarm <1|2>\n"
      "  bypass <zone 1-32>      toggle bypass\n"
      "  pgm <1-16> <on|off>\n"
      "  zone <1-32> <auto|enabled|disabled> [name]   local only, never sent to the panel\n"
      "  reread                  re-read the panel configuration (EEPROM reads only)\n"
      "  panel <connect|disconnect>  close / reopen the panel session\n"
      "  beep\n"
      "  time                    set panel clock from NTP\n"
      "  raw <on|off>            hex dump serial frames\n"
      "  config                  show saved settings (passwords hidden)\n"
      "  config reset            erase saved settings and reboot to config.h defaults\n"
      "  reboot");
}

static void printStatus() {
  const auto& i = panel.info();
  NetInfo n = netInfo();
  Serial.printf("WiFi: %s %s  AP: %s %s\n", n.staConnected ? n.ssid.c_str() : "-", n.ip.c_str(),
                n.apActive ? cfg.apSsid.c_str() : "off", n.apIp.c_str());
  Serial.printf("Link: %s %s  rx=%lu tx=%lu bad=%lu\n", paradox::linkStateName(panel.state()),
                panel.lastError(), (unsigned long)panel.framesRx(), (unsigned long)panel.framesTx(),
                (unsigned long)panel.checksumErrors());
  if (i.valid)
    Serial.printf("Panel: %s fw %u.%u.%u id %02X%02X  alarm=%d\n", i.model(), i.fwVersion,
                  i.fwRevision, i.fwBuild, i.panelId[0], i.panelId[1], panel.systemAlarm());
  for (int p = 0; p < paradox::kPartitions; p++) {
    const auto& a = panel.partition(p);
    Serial.printf("Area %d '%s': %s%s%s\n", p + 1, a.label, a.modeName(), a.ready() ? " ready" : "",
                  a.inAlarm() ? " ALARM" : "");
  }
  const char* t[32];
  int nt = listTroubles(panel.system(), t, 32);
  for (int k = 0; k < nt; k++) Serial.printf("Trouble: %s\n", t[k]);
  if (panel.lastAction()[0]) Serial.printf("Last action: %s\n", panel.lastAction());
}

static void printZones() {
  for (int z = 0; z < paradox::kZones; z++) {
    const auto& s = panel.zone(z);
    Serial.printf("%2d %-8s %-16s %-22s area %u  %s%s%s%s%s sig=%u\n", z + 1,
                  zoneEnabled(z) ? zoneModeName(zoneMode(z)) : "OFF", zoneName(z),
                  paradox::zoneDefinitionName(s.definition), s.partitionMask(), s.open ? "OPEN " : "closed",
                  s.alarm() ? " ALARM" : "", s.bypassed() ? " bypassed" : "",
                  s.tamper ? " tamper" : "", s.lowBattery ? " lowbatt" : "", s.signal);
  }
}

static void printEvents() {
  char text[96];
  for (int i = panel.eventCount() - 1; i >= 0; i--) {
    const auto& e = panel.event(i);
    describeEvent(panel, e, text, sizeof text);
    Serial.printf("%04u-%02u-%02u %02u:%02u  %s\n", e.year, e.month, e.day, e.hour, e.minute, text);
  }
}

static void report(bool ok) { Serial.println(ok ? "queued" : "failed (panel offline, busy or bad argument)"); }

static void handleCommand(String line) {
  line.trim();
  if (line.isEmpty()) return;
  char cmd[16] = "", a1[16] = "", a2[16] = "";
  sscanf(line.c_str(), "%15s %15s %15s", cmd, a1, a2);
  String c(cmd);
  int n1 = atoi(a1);

  if (c == "help" || c == "?") printHelp();
  else if (c == "status") printStatus();
  else if (c == "zones") printZones();
  else if (c == "events") printEvents();
  else if (c == "json") {
    String s;
    buildStatusJson(s);
    Serial.println(s);
  } else if (c == "arm" && (n1 == 1 || n1 == 2)) {
    String m(a2);
    if (m == "stay") report(panel.armStay(n1 - 1));
    else if (m == "sleep") report(panel.armSleep(n1 - 1));
    else if (m == "full" || m.isEmpty()) report(panel.armFull(n1 - 1));
    else Serial.println("mode must be full, stay or sleep");
  } else if (c == "disarm" && (n1 == 1 || n1 == 2)) report(panel.disarm(n1 - 1));
  else if (c == "bypass" && n1 >= 1 && n1 <= 32) report(panel.bypass(n1 - 1));
  else if (c == "pgm" && n1 >= 1 && n1 <= 16) {
    String m(a2);
    report(m == "on" ? panel.pgmOn(n1 - 1) : m == "off" ? panel.pgmOff(n1 - 1) : false);
  } else if (c == "zone" && n1 >= 1 && n1 <= 32) {
    String m(a2);
    ZoneMode mode = m == "enabled" ? ZoneMode::Enabled : m == "disabled" ? ZoneMode::Disabled : ZoneMode::Auto;
    if (m != "auto" && m != "enabled" && m != "disabled") {
      Serial.println("mode must be auto, enabled or disabled");
      return;
    }
    // Name = everything after the mode word.
    String name;
    int pos = line.indexOf(a2);
    if (pos >= 0) {
      name = line.substring(pos + m.length());
      name.trim();
    }
    Serial.println(zoneSet(n1 - 1, name, mode, zoneSensor(n1 - 1)) ? "saved (bridge only)" : "name too long (max 32)");
  } else if (c == "panel" && String(a1) == "disconnect") {
    panel.disconnect();
    Serial.println("panel disconnected (type 'panel connect' to reconnect)");
  } else if (c == "panel" && String(a1) == "connect") {
    panel.connect();
    Serial.println("connecting");
  } else if (c == "reread") {
    report(panel.rereadConfig());
  } else if (c == "beep") report(panel.beep());
  else if (c == "time") {
    if (!netInfo().timeValid) {
      Serial.println("no NTP time yet");
      return;
    }
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    report(panel.setTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min));
  } else if (c == "config" && String(a1) == "reset") {
    settingsReset();
    Serial.println("settings erased, rebooting");
    delay(300);
    ESP.restart();
  } else if (c == "config") {
    Serial.printf(
        "wifi ssid '%s'  ap ssid '%s'  hostname '%s'  web user '%s'\n"
        "uart rx %u tx %u  login %s  labels %d\n"
        "ntp '%s'  tz '%s'  display %d\n",
        cfg.wifiSsid.c_str(), cfg.apSsid.c_str(), cfg.hostname.c_str(), cfg.webUser.c_str(),
        cfg.rxPin, cfg.txPin, cfg.loginMethod == 1 ? "user code" : "PC password", cfg.readLabels, cfg.ntpServer.c_str(),
        cfg.tz.c_str(), cfg.displayEnabled);
  } else if (c == "reboot") {
    ESP.restart();
  } else if (c == "raw") {
    panel.rawDump = String(a1) == "on";
    Serial.printf("raw dump %s\n", panel.rawDump ? "on" : "off");
  } else {
    Serial.println("unknown command, type 'help'");
  }
}

static void consoleLoop() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      handleCommand(line);
      line = "";
    } else if (line.length() < 80) {
      line += ch;
    }
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Paradox serial bridge v" FW_VERSION " ===");

  settingsLoad();
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  Serial.printf("Board: %s\n", cfg.displayEnabled ? "TTGO T-Display (display on)" : "generic ESP32 (display off)");

  displayBegin();  // does nothing when the display is disabled

  Serial2.begin(PARADOX_BAUD, SERIAL_8N1, cfg.rxPin, cfg.txPin);
  panel.begin(Serial2);
  zonesBegin(panel);  // local zone settings + cached panel configuration
  panel.onEvent = [](const paradox::Event& e) {
    char text[96];
    describeEvent(panel, e, text, sizeof text);
    Serial.printf("[event] %02u:%02u %s (group %u sub %u area %u)\n", e.hour, e.minute, text, e.group,
                  e.sub, e.partition);
    mqttPublishEvent(e);
  };

  netBegin();
  webBegin(panel);
  mqttBegin(panel);
  homekitBegin(panel);  // only if chosen in Settings -> Smart home
  passthroughBegin(panel, Serial2);  // BabyWare pass-through (off until switched on)
  otaBegin();
  Serial.println("Type 'help' for console commands.");
}

// Recovery on any ESP32 board: press BOOT (GPIO0) within 5 s after power-on / restart
// and hold it for 10 s to erase saved settings and reboot with the config.h defaults.
// A press later during normal operation never resets.
static void resetButtonLoop() {
  static uint32_t downSince = 0;
  static uint32_t lastShown = 0;
  if (digitalRead(BUTTON_RESET) == LOW) {
    if (!downSince) {
      if (millis() > RESET_WINDOW_MS) return;  // not at start-up: ignore
      downSince = millis() | 1;
      Serial.println("[settings] reset button held at start-up - keep holding 10 s to erase settings");
    }
    uint32_t held = millis() - downSince;
    if (held / 1000 != lastShown) {
      lastShown = held / 1000;
      char msg[40];
      snprintf(msg, sizeof msg, "Hold for reset: %lu s", (unsigned long)((RESET_HOLD_MS - held + 999) / 1000));
      displayMessage(msg);
    }
    if (held >= RESET_HOLD_MS) {
      Serial.println("[settings] reset button held - erasing settings and rebooting");
      settingsReset();
      displayMessage("Settings reset - rebooting");
      delay(1500);
      ESP.restart();
    }
  } else if (downSince) {
    downSince = 0;
    lastShown = 0;
    Serial.println("[settings] reset cancelled (button released)");
    displayMessage("Reset cancelled");
  }
}

void loop() {
  resetButtonLoop();
  netLoop();
  passthroughLoop();
  webLoop();
  consoleLoop();
  if (passthroughMode()) {
    // BabyWare mode: panel driver, MQTT, HomeKit, network OTA and display pages stay
    // stopped until the bridge reboots.
    displayLoop(panel);  // backlight button only (notice is frozen)
    return;
  }
  panel.loop();
  otaLoop();
  mqttLoop();
  homekitLoop();
  displayLoop(panel);
}
