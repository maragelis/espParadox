#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <nvs.h>
#include <time.h>

#include "config.h"
#include "events.h"
#include "net.h"
#include "display.h"
#include "homekit.h"
#include "passthrough.h"
#include "mqtt.h"
#include "ota.h"
#include "settings.h"
#include "version.h"
#include <Update.h>
#include "manual_html.h"
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
  d["staticIp"] = cfg.staticIp;
  d["ip"] = cfg.ip;
  d["subnet"] = cfg.subnet;
  d["gateway"] = cfg.gateway;
  d["dns1"] = cfg.dns1;
  d["dns2"] = cfg.dns2;
  {
    NetInfo ni = netInfo();  // what the bridge uses right now (for "Use current values")
    JsonObject cur = d["current"].to<JsonObject>();
    cur["connected"] = ni.staConnected;
    cur["ip"] = ni.ip;
    cur["subnet"] = ni.subnet;
    cur["gateway"] = ni.gateway;
    cur["dns1"] = ni.dns1;
    cur["dns2"] = ni.dns2;
  }
  d["webUser"] = cfg.webUser;
  d["rxPin"] = cfg.rxPin;
  d["txPin"] = cfg.txPin;
  d["pcPasswordSet"] = cfg.pcPassword.length() > 0;
  d["loginMethod"] = cfg.loginMethod;
  d["userCodeSet"] = cfg.userCode.length() > 0;
  d["readLabels"] = cfg.readLabels;
  d["ntpServer"] = cfg.ntpServer;
  d["tz"] = cfg.tz;
  d["displayEnabled"] = cfg.displayEnabled;
  d["mqttEnabled"] = cfg.mqttEnabled;
  d["integration"] = cfg.homekitEnabled ? "homekit" : cfg.mqttEnabled ? "mqtt" : "none";
  d["passthroughPort"] = cfg.passthroughPort;
  d["ip150PasswordSet"] = cfg.ip150Password.length() > 0;
  d["mqttHost"] = cfg.mqttHost;
  d["mqttPort"] = cfg.mqttPort;
  d["mqttUser"] = cfg.mqttUser;
  d["mqttPasswordSet"] = cfg.mqttPassword.length() > 0;
  d["mqttBase"] = cfg.mqttBase;
  d["mqttAllowControl"] = cfg.mqttAllowControl;
  d["mqttRetainZones"] = cfg.mqttRetainZones;
  d["mqttRetainPanel"] = cfg.mqttRetainPanel;
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

// Parses a whole number: decimal ("8339"), or hex with a 0x prefix ("0x2093").
// Returns false if it isn't a number in [lo, hi].
bool parseNum(String s, long lo, long hi, long& out) {
  s.trim();
  if (s.isEmpty()) return false;
  bool hex = s.startsWith("0x") || s.startsWith("0X");
  char* end = nullptr;
  out = strtol(s.c_str() + (hex ? 2 : 0), &end, hex ? 16 : 10);
  return end && *end == '\0' && out >= lo && out <= hi;
}

void applySettings(const Settings& n, const char* how) {
  cfg = n;
  settingsSave();
  Serial.printf("[settings] %s, rebooting\n", how);
  restartAtMs = millis() + 1500;
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
  n.staticIp = arg("ipMode") == "static";
  n.ip = arg("ip");
  n.subnet = arg("subnet");
  n.gateway = arg("gateway");
  n.dns1 = arg("dns1");
  n.dns2 = arg("dns2");
  n.webUser = arg("webUser");
  secret("webPassword", n.webPassword);
  String pc = arg("pcPassword");
  if (pc.length()) n.pcPassword = pc;
  n.loginMethod = arg("loginMethod") == "1" ? 1 : 0;
  String uc = arg("userCode");
  if (uc.length()) n.userCode = uc;
  n.readLabels = server.arg("readLabels") == "on";
  n.ntpServer = arg("ntpServer");
  n.tz = arg("tz");
  n.displayEnabled = server.arg("displayEnabled") == "on";
  String integ = arg("integration");  // none | mqtt | homekit - only one at a time
  n.mqttEnabled = integ == "mqtt";
  n.homekitEnabled = integ == "homekit";
  long ptPort;
  if (!parseNum(arg("passthroughPort"), 1, 65535, ptPort)) return reply(false, "BabyWare pass-through port must be a number");
  n.passthroughPort = ptPort;
  secret("ip150Password", n.ip150Password);  // empty = keep
  n.mqttHost = arg("mqttHost");
  n.mqttUser = arg("mqttUser");
  secret("mqttPassword", n.mqttPassword);
  if (server.arg("mqttPasswordClear") == "on") n.mqttPassword = "";
  n.mqttBase = arg("mqttBase");
  while (n.mqttBase.endsWith("/")) n.mqttBase.remove(n.mqttBase.length() - 1);
  n.mqttAllowControl = server.arg("mqttAllowControl") == "on";
  n.mqttRetainZones = server.arg("mqttRetainZones") == "on";
  n.mqttRetainPanel = server.arg("mqttRetainPanel") == "on";
  n.haDiscovery = server.arg("haDiscovery") == "on";
  n.haPrefix = arg("haPrefix");
  while (n.haPrefix.endsWith("/")) n.haPrefix.remove(n.haPrefix.length() - 1);
  n.otaNetwork = server.arg("otaNetwork") == "on";

  long rx, tx, mport;
  if (!parseNum(arg("rxPin"), 0, 39, rx)) return reply(false, "Invalid RX pin");
  if (!parseNum(arg("txPin"), 0, 39, tx)) return reply(false, "Invalid TX pin");
  if (!parseNum(arg("mqttPort"), 1, 65535, mport)) return reply(false, "MQTT port must be 1-65535");
  n.rxPin = rx;
  n.txPin = tx;
  n.mqttPort = mport;
  if (const char* err = settingsValidate(n)) return reply(false, err);

  String msg = "Saved. Rebooting - reconnect in about 10 seconds";
  if (n.staticIp && (!cfg.staticIp || n.ip != cfg.ip)) msg += " at http://" + n.ip + "/";
  else if (!n.staticIp && cfg.staticIp) msg += " - the router will give it a new address (try http://" + n.hostname + ".local/)";
  msg += ".";
  reply(true, msg.c_str());
  applySettings(n, "saved from web");
}

// ---- change the web password (no reboot) ------------------------------------------

void handlePassword() {
  if (!auth()) return;
  String current = server.arg("current"), pw = server.arg("new"), confirm = server.arg("confirm");
  if (current != cfg.webPassword) return reply(false, "The current password is wrong");
  if (pw.length() < 6) return reply(false, "The new password must be at least 6 characters");
  if (pw.length() > 64) return reply(false, "The new password is too long (max 64)");
  if (pw != confirm) return reply(false, "The two new passwords don't match");
  if (pw == current) return reply(false, "The new password is the same as the current one");
  cfg.webPassword = pw;
  settingsSave();
  otaSetPassword(cfg.webPassword.c_str());
  Serial.println("[settings] web password changed");
  reply(true, "Password changed. Your browser will ask you to log in again with the new password.");
}

// ---- backup / restore -----------------------------------------------------------

void handleBackup() {
  if (!auth()) return;
  JsonDocument d;
  d["type"] = "espParadox-backup";
  d["firmware"] = FW_VERSION;
  if (time(nullptr) > 1700000000) {
    char when[24];
    time_t now = time(nullptr);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
    d["created"] = when;
  }
  settingsToJson(d["settings"].to<JsonObject>());
  JsonArray zones = d["zones"].to<JsonArray>();
  for (int z = 0; z < paradox::kZones; z++) {
    JsonObject o = zones.add<JsonObject>();
    o["id"] = z + 1;
    o["name"] = zoneCustomLabel(z);
    o["mode"] = zoneModeName(zoneMode(z));
    o["sensor"] = zoneSensorName(zoneSensor(z));
  }
  String s;
  serializeJsonPretty(d, s);
  String file = "paradox-bridge-" + cfg.hostname + "-backup.json";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + file + "\"");
  server.send(200, "application/json", s);
}

void handleRestore() {
  if (!auth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return reply(false, "That file isn't valid JSON");
  if (d["type"] != "espParadox-backup") return reply(false, "That file isn't a Paradox bridge backup");
  Settings n = cfg;
  settingsFromJson(d["settings"].as<JsonObjectConst>(), n);
  if (const char* err = settingsValidate(n)) return reply(false, (String("Backup not restored: ") + err).c_str());
  int zonesRestored = 0;
  for (JsonObjectConst o : d["zones"].as<JsonArrayConst>()) {
    int z = o["id"] | 0;
    if (z < 1 || z > paradox::kZones) continue;
    String m = o["mode"] | "auto";
    ZoneMode mode = m == "enabled" ? ZoneMode::Enabled : m == "disabled" ? ZoneMode::Disabled : ZoneMode::Auto;
    ZoneSensor sensor = String(o["sensor"] | "contact") == "motion" ? ZoneSensor::Motion : ZoneSensor::Contact;
    String name = o["name"] | "";
    if (name.length() > kCustomLabelLen) name = name.substring(0, kCustomLabelLen);
    zoneSet(z - 1, name, mode, sensor);
    zonesRestored++;
  }
  String msg = "Backup restored (settings + " + String(zonesRestored) + " zones). Rebooting - reconnect in about 10 seconds.";
  reply(true, msg.c_str());
  applySettings(n, "restored from backup");
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
  d["defaultPassword"] = cfg.webPassword == WEB_PASSWORD;  // still the factory password
  d["displayEnabled"] = cfg.displayEnabled;
  d["mqtt"] = mqttStatus();
  d["homekit"] = homekitInfo().status;
  {
    PassthroughInfo pt = passthroughInfo();
    JsonObject o = d["passthrough"].to<JsonObject>();
    o["enabled"] = pt.enabled;
    o["mode"] = pt.enabled;  // BabyWare mode: normal services stopped
    o["connected"] = pt.connected;
    o["secondsLeft"] = pt.secondsLeft;
    o["port"] = pt.port;
    o["client"] = pt.client;
    o["rfc2217"] = pt.rfc2217;
    o["protocol"] = pt.protocol;
    o["ip150State"] = pt.ip150State;
    o["baud"] = pt.baud;
    o["bytesToPanel"] = pt.bytesToPanel;
    o["bytesFromPanel"] = pt.bytesFromPanel;
    o["firstToPanel"] = pt.firstToPanel;
    o["firstFromPanel"] = pt.firstFromPanel;
  }
  d["uptime"] = millis() / 1000;
  {
    // RAM is what matters at runtime (low heap = crashes); flash only for updates / settings.
    JsonObject m = d["memory"].to<JsonObject>();
    m["heapFree"] = ESP.getFreeHeap();
    m["heapMin"] = ESP.getMinFreeHeap();        // lowest since start
    m["heapMaxBlock"] = ESP.getMaxAllocHeap();  // largest single block
    m["heapTotal"] = ESP.getHeapSize();
    nvs_stats_t ns;
    if (nvs_get_stats(NULL, &ns) == ESP_OK) {
      m["nvsUsed"] = ns.used_entries;
      m["nvsTotal"] = ns.total_entries;
    }
    static uint32_t sketchSize = ESP.getSketchSize();  // slow to compute: once
    m["firmwareSize"] = sketchSize;
    const esp_partition_t* run = esp_ota_get_running_partition();
    m["firmwareSlot"] = run ? run->size : 0;
  }

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
  link["loginMethod"] = cfg.loginMethod == 1 ? "user code" : "PC password";
  link["loginUser"] = p.loginUser();
  link["loginAreas"] = p.loginAreaRights();
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
  // The manual needs no login: it holds no settings, and it explains how to recover a forgotten password.
  server.on("/manual", HTTP_GET, [] { server.send_P(200, "text/html", kManualHtml); });
  server.on("/settings", HTTP_GET, [] {
    if (!auth()) return;
    server.send_P(200, "text/html", kSettingsHtml);
  });
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/zone", HTTP_POST, handleZone);
  // ---- HomeKit ----
  server.on("/api/homekit", HTTP_GET, [] {
    if (!auth()) return;
    HomekitInfo h = homekitInfo();
    JsonDocument d;
    d["enabled"] = h.enabled;
    d["started"] = h.started;
    d["paired"] = h.paired;
    d["controllers"] = h.controllers;
    d["accessories"] = h.accessories;
    d["code"] = h.code;
    d["setupUri"] = h.setupUri;
    d["host"] = h.host;
    d["port"] = h.port;
    d["status"] = h.status;
    String s;
    serializeJson(d, s);
    server.send(200, "application/json", s);
  });
  server.on("/api/homekit/qr.svg", HTTP_GET, [] {
    if (!auth()) return;
    String svg = homekitQrSvg();
    if (svg.isEmpty()) return server.send(404, "text/plain", "HomeKit is off");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "image/svg+xml", svg);
  });
  server.on("/api/homekit/unpair", HTTP_POST, [] {
    if (!auth()) return;
    bool ok = homekitUnpair();
    reply(ok, ok ? "All HomeKit pairings removed - add the bridge in the Home app again" : "HomeKit is not running");
  });
  server.on("/api/homekit/newcode", HTTP_POST, [] {
    if (!auth()) return;
    if (!cfg.homekitEnabled) return reply(false, "HomeKit is off");
    homekitNewCode();
    reply(true, "New setup code created");
  });
  // Old Home Assistant discovery entries (other Paradox integrations / earlier bridges)
  server.on("/api/mqtt/ha-scan", HTTP_GET, [] {
    if (!auth()) return;
    std::vector<HaStaleEntry> list;
    String err;
    if (!mqttHaScan(list, err)) return reply(false, err.c_str());
    JsonDocument d;
    d["ok"] = true;
    JsonArray a = d["items"].to<JsonArray>();
    for (const auto& e : list) {
      JsonObject o = a.add<JsonObject>();
      o["topic"] = e.topic;
      o["name"] = e.name;
      o["node"] = e.node;
    }
    String s;
    serializeJson(d, s);
    server.send(200, "application/json", s);
  });
  server.on("/api/mqtt/ha-cleanup", HTTP_POST, [] {
    if (!auth()) return;
    std::vector<HaStaleEntry> list;
    String err;
    if (!mqttHaScan(list, err)) return reply(false, err.c_str());  // scan again: delete only what is there now
    int n = mqttHaDelete(list);
    String msg = "Removed " + String(n) + " old Home Assistant entit" + (n == 1 ? "y" : "ies") +
                 ". This bridge's own device was kept.";
    reply(true, msg.c_str());
  });
  // ---- BabyWare pass-through ----
  server.on("/api/passthrough/enable", HTTP_POST, [] {
    if (!auth()) return;
    long minutes = server.arg("minutes").toInt();
    if (minutes < 1 || minutes > 240) minutes = 30;
    passthroughEnable(minutes);
    String msg = "BabyWare mode on - connect BabyWare to " + WiFi.localIP().toString() + " port " +
                 String(cfg.passthroughPort) + ". Everything else is stopped; reboot to return (automatic after " +
                 String(minutes) + " min without BabyWare).";
    reply(true, msg.c_str());
  });
  server.on("/api/passthrough/disable", HTTP_POST, [] {
    if (!auth()) return;
    reply(true, "Leaving BabyWare mode - rebooting. Reconnect in about 15 seconds.");
    restartAtMs = millis() + 1500;
  });
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
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/config/backup", HTTP_GET, handleBackup);
  server.on("/api/config/restore", HTTP_POST, handleRestore);
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
