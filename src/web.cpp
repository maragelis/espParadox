#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <time.h>

#include "config.h"
#include "events.h"
#include "net.h"
#include "display.h"
#include "mqtt.h"
#include "ota.h"
#include "settings.h"
#include "version.h"
#include <Update.h>
#include "web_ui.h"
#include "zones.h"

namespace {

WebServer server(80);
paradox::Panel* panel = nullptr;
uint32_t restartAtMs = 0;  // non-zero: reboot at this time (after the reply is sent)

bool auth() {
  if (server.authenticate(cfg.webUser.c_str(), cfg.webPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Paradox bridge");
  return false;
}

void reply(bool ok, const char* msg) {
  JsonDocument d;
  d["ok"] = ok;
  d["message"] = msg;
  String s;
  serializeJson(d, s);
  server.send(ok ? 200 : 400, "application/json", s);
}

void handleAction() {
  if (!auth()) return;
  String a = server.arg("action");
  int part = server.hasArg("partition") ? server.arg("partition").toInt() : 1;
  int zone = server.arg("zone").toInt();
  int pgm = server.arg("pgm").toInt();
  if (part < 1 || part > paradox::kPartitions) return reply(false, "partition must be 1 or 2");
  uint8_t p = part - 1;

  bool ok;
  if (a == "arm_stay") ok = panel->armStay(p);
  else if (a == "arm_sleep") ok = panel->armSleep(p);
  else if (a == "arm_full") ok = panel->armFull(p);
  else if (a == "arm_stay_d") ok = panel->armStayD(p);
  else if (a == "arm_sleep_d") ok = panel->armSleepD(p);
  else if (a == "disarm") ok = panel->disarm(p);
  else if (a == "disarm_all") ok = panel->disarmAllStayDOff();
  else if (a == "beep") ok = panel->beep(0x04);
  else if (a == "bypass") {
    if (zone < 1 || zone > paradox::kZones) return reply(false, "zone must be 1-32");
    if (!zoneEnabled(zone - 1)) return reply(false, "zone is disabled");
    ok = panel->bypass(zone - 1);
  } else if (a == "pgm_on" || a == "pgm_off") {
    if (pgm < 1 || pgm > paradox::kPgms) return reply(false, "pgm must be 1-16");
    ok = (a == "pgm_on") ? panel->pgmOn(pgm - 1) : panel->pgmOff(pgm - 1);
  } else {
    return reply(false, "unknown action");
  }
  reply(ok, ok ? "queued" : "panel offline or busy");
}

void handleTime() {
  if (!auth()) return;
  if (!netInfo().timeValid) return reply(false, "no NTP time yet");
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  bool ok = panel->setTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
  reply(ok, ok ? "queued" : "panel offline or busy");
}

// ---- Settings -------------------------------------------------------------

void handleGetConfig() {
  if (!auth()) return;
  JsonDocument d;
  d["wifiSsid"] = cfg.wifiSsid;
  d["wifiPasswordSet"] = cfg.wifiPassword.length() > 0;
  d["apSsid"] = cfg.apSsid;
  d["apPasswordSet"] = cfg.apPassword.length() > 0;
  d["hostname"] = cfg.hostname;
  d["webUser"] = cfg.webUser;
  d["rxPin"] = cfg.rxPin;
  d["txPin"] = cfg.txPin;
  d["pcPasswordSet"] = cfg.pcPassword.length() > 0;
  d["sourceId"] = cfg.sourceId;
  d["userId"] = cfg.userId;
  d["readLabels"] = cfg.readLabels;
  d["ntpServer"] = cfg.ntpServer;
  d["tz"] = cfg.tz;
  d["displayEnabled"] = cfg.displayEnabled;
  d["mqttEnabled"] = cfg.mqttEnabled;
  d["mqttHost"] = cfg.mqttHost;
  d["mqttPort"] = cfg.mqttPort;
  d["mqttUser"] = cfg.mqttUser;
  d["mqttPasswordSet"] = cfg.mqttPassword.length() > 0;
  d["mqttBase"] = cfg.mqttBase;
  d["mqttAllowControl"] = cfg.mqttAllowControl;
  d["haDiscovery"] = cfg.haDiscovery;
  d["haPrefix"] = cfg.haPrefix;
  d["mqttStatus"] = mqttStatus();
  d["otaNetwork"] = cfg.otaNetwork;
  d["version"] = FW_VERSION;
  // Valid UART pins with the display on (T-Display) and off (other boards).
  for (int on = 0; on < 2; on++) {
    JsonObject o = d["pins"][on ? "display" : "generic"].to<JsonObject>();
    JsonArray rx = o["rx"].to<JsonArray>();
    JsonArray tx = o["tx"].to<JsonArray>();
    for (int p = 0; p < 40; p++) {
      if (settingsRxPinValid(p, on)) rx.add(p);
      if (settingsTxPinValid(p, on)) tx.add(p);
    }
  }
  String s;
  serializeJson(d, s);
  server.send(200, "application/json", s);
}

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

// Parses "12", "0x0C" etc. Returns false if not a number in [lo, hi].
bool parseNum(const String& s, long lo, long hi, long& out) {
  if (s.isEmpty()) return false;
  char* end = nullptr;
  out = strtol(s.c_str(), &end, 0);
  return end && *end == '\0' && out >= lo && out <= hi;
}

void handleSetConfig() {
  if (!auth()) return;
  Settings n = cfg;  // validate everything before touching the live settings
  auto arg = [](const char* k) {
    String v = server.arg(k);
    v.trim();
    return v;
  };
  // Password fields: left empty = keep the current value.
  auto secret = [](const char* k, String& dst) {
    String v = server.arg(k);
    if (v.length()) dst = v;
  };

  n.wifiSsid = arg("wifiSsid");
  secret("wifiPassword", n.wifiPassword);
  if (server.arg("wifiPasswordClear") == "on") n.wifiPassword = "";
  n.apSsid = arg("apSsid");
  secret("apPassword", n.apPassword);
  n.hostname = arg("hostname");
  n.webUser = arg("webUser");
  secret("webPassword", n.webPassword);
  String pc = arg("pcPassword");
  if (pc.length()) n.pcPassword = pc;
  n.readLabels = server.arg("readLabels") == "on";
  n.ntpServer = arg("ntpServer");
  n.tz = arg("tz");
  n.displayEnabled = server.arg("displayEnabled") == "on";
  n.mqttEnabled = server.arg("mqttEnabled") == "on";
  n.mqttHost = arg("mqttHost");
  n.mqttUser = arg("mqttUser");
  secret("mqttPassword", n.mqttPassword);
  if (server.arg("mqttPasswordClear") == "on") n.mqttPassword = "";
  n.mqttBase = arg("mqttBase");
  while (n.mqttBase.endsWith("/")) n.mqttBase.remove(n.mqttBase.length() - 1);
  n.mqttAllowControl = server.arg("mqttAllowControl") == "on";
  n.haDiscovery = server.arg("haDiscovery") == "on";
  n.haPrefix = arg("haPrefix");
  while (n.haPrefix.endsWith("/")) n.haPrefix.remove(n.haPrefix.length() - 1);
  n.otaNetwork = server.arg("otaNetwork") == "on";

  long rx, tx, src, uid, mport;
  if (n.mqttEnabled && n.mqttHost.isEmpty()) return reply(false, "MQTT broker address is required");
  if (n.mqttHost.length() > 64) return reply(false, "MQTT broker address is too long");
  if (!parseNum(arg("mqttPort"), 1, 65535, mport)) return reply(false, "MQTT port must be 1-65535");
  if (n.mqttBase.isEmpty() || n.mqttBase.length() > 48 || n.mqttBase.indexOf('+') >= 0 ||
      n.mqttBase.indexOf('#') >= 0)
    return reply(false, "MQTT base topic: 1-48 characters, no + or #");
  if (n.haPrefix.isEmpty() || n.haPrefix.length() > 32 || n.haPrefix.indexOf('+') >= 0 || n.haPrefix.indexOf('#') >= 0)
    return reply(false, "Home Assistant discovery prefix: 1-32 characters, no + or #");
  n.mqttPort = mport;
  if (n.wifiSsid.length() > 32) return reply(false, "WiFi name is longer than 32 characters");
  if (n.wifiPassword.length() && (n.wifiPassword.length() < 8 || n.wifiPassword.length() > 63))
    return reply(false, "WiFi password must be 8-63 characters");
  if (n.apSsid.isEmpty() || n.apSsid.length() > 32) return reply(false, "Access point name must be 1-32 characters");
  if (n.apPassword.length() < 8 || n.apPassword.length() > 63)
    return reply(false, "Access point password must be 8-63 characters");
  if (!isHostname(n.hostname)) return reply(false, "Hostname: letters, digits and '-' only (max 32)");
  if (n.webUser.isEmpty()) return reply(false, "Web user can't be empty");
  if (n.webPassword.length() < 4) return reply(false, "Web password must be at least 4 characters");
  if (!parseNum(arg("rxPin"), 0, 39, rx) || !settingsRxPinValid(rx, n.displayEnabled))
    return reply(false, n.displayEnabled && settingsPinReservedByDisplay(rx)
                            ? "RX pin is used by the display - pick another or turn the display off"
                            : "Invalid RX pin");
  if (!parseNum(arg("txPin"), 0, 39, tx) || !settingsTxPinValid(tx, n.displayEnabled))
    return reply(false, n.displayEnabled && settingsPinReservedByDisplay(tx)
                            ? "TX pin is used by the display - pick another or turn the display off"
                            : "Invalid TX pin");
  if (rx == tx) return reply(false, "RX and TX must be different pins");
  if (!isHex4(n.pcPassword)) return reply(false, "PC password must be exactly 4 digits (0-9, A-F)");
  if (!parseNum(arg("sourceId"), 1, 0x0C, src)) return reply(false, "Source ID must be 1-12 (0x01-0x0C)");
  if (!parseNum(arg("userId"), 0, 0xFFFF, uid)) return reply(false, "User ID must be 0-65535 (0x0000-0xFFFF)");
  if (n.ntpServer.isEmpty() || n.ntpServer.length() > 64) return reply(false, "NTP server must be 1-64 characters");
  if (n.tz.isEmpty() || n.tz.length() > 64) return reply(false, "Time zone must be 1-64 characters");

  n.rxPin = rx;
  n.txPin = tx;
  n.sourceId = src;
  n.userId = uid;
  cfg = n;
  settingsSave();
  Serial.println("[settings] saved from web, rebooting");
  reply(true, "Saved. Rebooting - reconnect in about 10 seconds.");
  restartAtMs = millis() + 1500;
}

// ---- OTA: firmware upload from the Settings page ---------------------------
bool uploadAuthorized = false;
String uploadError;

void handleUpdateChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadError = "";
    uploadAuthorized = server.authenticate(cfg.webUser.c_str(), cfg.webPassword.c_str());
    if (!uploadAuthorized) return;
    Serial.printf("[ota] web upload: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) uploadError = Update.errorString();
  } else if (!uploadAuthorized || uploadError.length()) {
    if (up.status == UPLOAD_FILE_ABORTED || up.status == UPLOAD_FILE_END) Update.abort();
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) uploadError = Update.errorString();
    else otaProgress("web", Update.progress(), server.header("Content-Length").toInt());
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) uploadError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    uploadError = "upload aborted";
  }
}

void handleUpdateDone() {
  if (!auth()) return;
  if (!uploadAuthorized) return reply(false, "not authorized");
  if (uploadError.length()) {
    Serial.printf("[ota] web upload failed: %s\n", uploadError.c_str());
    displayMessage("Update FAILED");
    return reply(false, ("Update failed: " + uploadError).c_str());
  }
  Serial.println("[ota] web upload OK, rebooting");
  displayMessage("Update done - rebooting");
  reply(true, "Update installed. Rebooting - reconnect in about 15 seconds.");
  restartAtMs = millis() + 1500;
}

// Local zone settings - stored on the board only, never sent to the panel.
void handleZone() {
  if (!auth()) return;
  int z = server.arg("zone").toInt();
  if (z < 1 || z > paradox::kZones) return reply(false, "zone must be 1-32");
  String label = server.arg("label");
  label.trim();
  if (label.length() > kCustomLabelLen) return reply(false, "name is longer than 32 characters");
  String m = server.arg("mode");
  ZoneMode mode;
  if (m == "auto") mode = ZoneMode::Auto;
  else if (m == "enabled") mode = ZoneMode::Enabled;
  else if (m == "disabled") mode = ZoneMode::Disabled;
  else return reply(false, "mode must be auto, enabled or disabled");
  String st = server.arg("sensor");
  ZoneSensor sensor = st == "motion" ? ZoneSensor::Motion : st == "contact" || st.isEmpty() ? ZoneSensor::Contact : (ZoneSensor)0xFF;
  if ((uint8_t)sensor == 0xFF) return reply(false, "sensor must be contact or motion");
  zoneSet(z - 1, label, mode, sensor);
  reply(true, "Zone saved (on the bridge only)");
}

void handleScan() {
  if (!auth()) return;
  int n = WiFi.scanNetworks();  // blocks ~2-3 s
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    bool dup = false;
    for (JsonVariant v : arr)
      if (WiFi.SSID(i) == v["ssid"].as<String>()) dup = true;
    if (dup || WiFi.SSID(i).isEmpty()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String s;
  serializeJson(d, s);
  server.send(200, "application/json", s);
}

}  // namespace

void buildStatusJson(String& out) {
  const auto& p = *panel;
  const auto& s = p.system();
  const auto& info = p.info();
  JsonDocument d;
  char buf[96];

  d["version"] = FW_VERSION;
  d["displayEnabled"] = cfg.displayEnabled;
  d["mqtt"] = mqttStatus();
  d["uptime"] = millis() / 1000;

  NetInfo n = netInfo();
  JsonObject wifi = d["wifi"].to<JsonObject>();
  wifi["connected"] = n.staConnected;
  wifi["ssid"] = n.ssid;
  wifi["ip"] = n.ip;
  wifi["rssi"] = n.rssi;
  wifi["ap"] = n.apActive;
  wifi["apIp"] = n.apIp;

  JsonObject link = d["link"].to<JsonObject>();
  link["state"] = paradox::linkStateName(p.state());
  link["error"] = p.lastError();
  link["stopped"] = p.stopped();
  link["lastAction"] = p.lastAction();
  link["framesRx"] = p.framesRx();
  link["framesTx"] = p.framesTx();
  link["checksumErrors"] = p.checksumErrors();
  link["countersAge"] = p.countersAgeSec();
  link["configReading"] = p.configReading();
  link["labelsValid"] = p.labelsValid();
  link["definitionsValid"] = p.definitionsValid();
  link["configCached"] = panelConfigCached();
  link["statusComplete"] = (s.pagesSeen & 0x3F) == 0x3F;

  JsonObject pi = d["panel"].to<JsonObject>();
  pi["detected"] = info.valid;
  pi["model"] = info.model();
  pi["productId"] = info.productId;
  snprintf(buf, sizeof buf, "%u.%u.%u", info.fwVersion, info.fwRevision, info.fwBuild);
  pi["firmware"] = buf;
  snprintf(buf, sizeof buf, "%02X%02X", info.panelId[0], info.panelId[1]);
  pi["panelId"] = buf;
  pi["alarm"] = p.systemAlarm();
  snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u", s.year, s.month, s.day, s.hour, s.minute);
  pi["time"] = buf;
  pi["acLevel"] = s.acLevel;
  pi["dcLevel"] = s.dcLevel;
  pi["batteryLevel"] = s.batteryLevel;
  pi["rfNoise"] = s.rfNoise;
  pi["acFailure"] = s.acFailure();
  pi["batteryLow"] = s.batteryLow();
  pi["moduleTamper"] = s.moduleTamper;
  pi["moduleSupervision"] = s.moduleSupervision;

  const char* troubles[32];
  int nt = listTroubles(s, troubles, 32);
  JsonArray tr = d["troubles"].to<JsonArray>();
  for (int i = 0; i < nt; i++) tr.add(troubles[i]);

  JsonArray parts = d["partitions"].to<JsonArray>();
  for (int i = 0; i < paradox::kPartitions; i++) {
    const auto& a = p.partition(i);
    JsonObject o = parts.add<JsonObject>();
    o["id"] = i + 1;
    o["label"] = a.label;
    o["inUse"] = partitionInUse(i);
    o["mode"] = a.modeName();
    o["armed"] = a.armed();
    o["ready"] = a.ready();
    o["alarm"] = a.inAlarm();
    o["audibleAlarm"] = a.audibleAlarm();
    o["silentAlarm"] = a.silentAlarm();
    o["fireAlarm"] = a.fireAlarm();
    o["strobe"] = a.strobe();
    o["entryDelay"] = a.entryDelay();
    o["exitDelay"] = a.exitDelay();
    o["alarmMemory"] = a.alarmMemory();
    o["zoneBypassed"] = a.zoneBypassed();
    o["bell"] = a.bellOn();
    o["stayD"] = a.stayD();
    o["forceArmed"] = a.forceArmed();
  }

  JsonArray zones = d["zones"].to<JsonArray>();
  for (int i = 0; i < paradox::kZones; i++) {
    const auto& z = p.zone(i);
    JsonObject o = zones.add<JsonObject>();
    o["id"] = i + 1;
    o["label"] = zoneName(i);  // custom name, else panel label
    o["panelLabel"] = z.label;
    o["customLabel"] = zoneCustomLabel(i);
    o["mode"] = zoneModeName(zoneMode(i));
    o["sensor"] = zoneSensorName(zoneSensor(i));
    o["enabled"] = zoneEnabled(i);
    o["panelEnabled"] = z.panelEnabled();
    o["definition"] = z.definition;
    o["definitionName"] = paradox::zoneDefinitionName(z.definition);
    o["partitions"] = z.partitionMask();
    o["partitionRaw"] = z.partitionRaw;
    o["alarmType"] = paradox::zoneAlarmTypeName(z.optAlarmType());
    o["optionsRaw"] = z.options;
    JsonArray zo = o["options"].to<JsonArray>();
    if (z.definitionKnown()) {
      if (z.optBypassable()) zo.add("bypassable");
      if (z.optForceArm()) zo.add("force arm");
      if (z.optAutoShutdown()) zo.add("auto shutdown");
      if (z.optRfSupervision()) zo.add("RF supervision");
      if (z.optIntellizone()) zo.add("intellizone");
      if (z.optDelayTransmission()) zo.add("delayed transmission");
    }
    o["open"] = z.open;
    o["alarm"] = z.alarm();
    o["wasAlarm"] = z.wasAlarm();
    o["bypassed"] = z.bypassed();
    o["tamper"] = z.tamper;
    o["fireTrouble"] = z.fireTrouble;
    o["lowBattery"] = z.lowBattery;
    o["supervision"] = z.rfSupervision;
    o["shutdown"] = z.shutdown();
    o["exitDelay"] = z.exitDelay;
    o["delay"] = z.delayName();
    o["signal"] = z.signal;
  }

  JsonArray pgms = d["pgms"].to<JsonArray>();
  for (int i = 0; i < paradox::kPgms; i++) {
    const auto& g = p.pgm(i);
    JsonObject o = pgms.add<JsonObject>();
    o["id"] = i + 1;
    o["label"] = g.label;
    o["tamper"] = g.tamper;
    o["supervision"] = g.rfSupervision;
    o["signal"] = g.signal;
  }

  JsonArray ev = d["events"].to<JsonArray>();
  for (int i = 0; i < p.eventCount(); i++) {
    const auto& e = p.event(i);
    JsonObject o = ev.add<JsonObject>();
    snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u", e.year, e.month, e.day, e.hour, e.minute);
    o["time"] = buf;
    o["group"] = e.group;
    o["sub"] = e.sub;
    o["partition"] = e.partition;
    describeEvent(p, e, buf, sizeof buf);
    o["text"] = buf;
    if (const char* who = eventUserName(p, e)) {
      o["user"] = who;
      o["userNumber"] = e.sub;
    }
  }

  serializeJson(d, out);
}

void webBegin(paradox::Panel& p) {
  panel = &p;
  server.on("/", HTTP_GET, [] {
    if (!auth()) return;
    server.send_P(200, "text/html", kIndexHtml);
  });
  server.on("/api/status", HTTP_GET, [] {
    if (!auth()) return;
    String s;
    buildStatusJson(s);
    server.send(200, "application/json", s);
  });
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/settings", HTTP_GET, [] {
    if (!auth()) return;
    server.send_P(200, "text/html", kSettingsHtml);
  });
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/zone", HTTP_POST, handleZone);
  server.on("/api/mqtt/republish", HTTP_POST, [] {
    if (!auth()) return;
    if (!cfg.mqttEnabled) return reply(false, "MQTT is disabled");
    bool ok = mqttRepublish();
    reply(ok, ok ? (cfg.haDiscovery ? "Home Assistant discovery and all states published"
                                    : "All states published (Home Assistant discovery is off)")
                 : (String("MQTT not connected: ") + mqttStatus()).c_str());
  });
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);
  const char* headers[] = {"Content-Length"};
  server.collectHeaders(headers, 1);
  server.on("/api/zones/reset", HTTP_POST, [] {
    if (!auth()) return;
    zonesResetLocal();
    reply(true, "All zone names and modes reset (panel labels and definitions apply)");
  });
  server.on("/api/panel/disconnect", HTTP_POST, [] {
    if (!auth()) return;
    panel->disconnect();
    reply(true, "Panel disconnected. The bridge won't reconnect until you press Connect (or reboot).");
  });
  server.on("/api/panel/connect", HTTP_POST, [] {
    if (!auth()) return;
    panel->connect();
    reply(true, "Connecting to the panel");
  });
  server.on("/api/panel/reread", HTTP_POST, [] {
    if (!auth()) return;
    bool ok = panel->rereadConfig();
    reply(ok, ok ? "Reading the panel configuration (read only)" : "panel offline or already reading");
  });
  server.on("/api/config/reset", HTTP_POST, [] {
    if (!auth()) return;
    settingsReset();
    reply(true, "Settings erased. Rebooting with the built-in defaults.");
    restartAtMs = millis() + 1500;
  });
  server.on("/api/reboot", HTTP_POST, [] {
    if (!auth()) return;
    reply(true, "Rebooting.");
    restartAtMs = millis() + 1000;
  });
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}
