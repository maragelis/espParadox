#include "mqtt.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "events.h"
#include "settings.h"
#include "version.h"
#include "zones.h"

namespace {

WiFiClient net;
PubSubClient client(net);
paradox::Panel* panel = nullptr;
uint32_t lastAttemptMs = 0, lastScanMs = 0;
char status[48] = "disabled";
char lastActionSeen[64] = "";

// Hash of the last published payload per topic slot, to publish only on change.
enum Slot { kLink, kAlarm, kTroubles, kInfo, kAcPower, kBatteryLow, kAreaBase };
constexpr int kAreaSlots = 6;  // state, homekit, target, ready, json, ha
constexpr int kZoneBase = kAreaBase + paradox::kPartitions * kAreaSlots;
constexpr int kDiscBase = kZoneBase + paradox::kZones * 2;  // zone: open, json
// Home Assistant discovery: areas, zones, link sensor, trouble sensor
constexpr int kDiscArea = kDiscBase;
constexpr int kDiscZone = kDiscArea + paradox::kPartitions;
constexpr int kDiscLink = kDiscZone + paradox::kZones;
constexpr int kDiscTroubles = kDiscLink + 1;
constexpr int kDiscAc = kDiscTroubles + 1;
constexpr int kDiscBattery = kDiscAc + 1;
constexpr int kSlots = kDiscBattery + 1;
uint32_t lastHash[kSlots];
bool zoneWasPublished[paradox::kZones];

uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h ? h : 1;
}

String topic(const char* suffix) { return cfg.mqttBase + "/" + suffix; }

void publish(const String& t, const String& payload, bool retain = true) {
  client.publish(t.c_str(), payload.c_str(), retain);
}

// Publish if the payload for this slot changed since last time.
void publishSlot(int slot, const String& t, const String& payload) {
  uint32_t h = fnv1a(payload.c_str());
  if (lastHash[slot] == h) return;
  lastHash[slot] = h;
  publish(t, payload);
}

const char* areaState(const paradox::Partition& a) { return a.inAlarm() ? "alarm" : a.modeName(); }

// Home Assistant alarm_control_panel state.
const char* haState(const paradox::Partition& a) {
  if (a.inAlarm()) return "triggered";
  if (a.entryDelay()) return "pending";
  if (a.exitDelay()) return "arming";
  if (!a.armed()) return "disarmed";
  if (a.stayArmed()) return "armed_home";
  if (a.sleepArmed()) return "armed_night";
  return "armed_away";
}

// ---- Home Assistant MQTT discovery -------------------------------------------

String nodeId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return "paradox_" + mac;
}

// needsPanel: entity is only available while the bridge AND the panel link are up.
void addDevice(JsonDocument& d, bool needsPanel = true) {
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"].to<JsonArray>().add(nodeId());
  dev["name"] = "Paradox alarm";
  dev["manufacturer"] = "Paradox";
  dev["model"] = panel->info().valid ? panel->info().model() : "MG/SP panel";
  dev["sw_version"] = FW_VERSION;
  dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
  JsonArray av = d["availability"].to<JsonArray>();
  JsonObject a1 = av.add<JsonObject>();
  a1["topic"] = topic("status");  // bridge: online / offline (last will)
  if (needsPanel) {
    JsonObject a2 = av.add<JsonObject>();
    a2["topic"] = topic("panel/link");
    a2["payload_available"] = "online";
    a2["payload_not_available"] = "disconnected";
    d["availability_mode"] = "all";
  }
}

String discTopic(const char* component, const String& object) {
  return cfg.haPrefix + "/" + component + "/" + nodeId() + "/" + object + "/config";
}

// Publish a discovery config (or remove it with an empty retained message).
void publishDiscovery(int slot, const String& t, JsonDocument* d) {
  if (!d) {
    // Remove the entity (empty retained config). Hash 1 marks "removed"; after a
    // (re)connect hashes are 0, so stale entities from before a reboot go too.
    if (lastHash[slot] != 1) {
      publish(t, "");
      lastHash[slot] = 1;
    }
    return;
  }
  String s;
  serializeJson(*d, s);
  publishSlot(slot, t, s);
}

// zonesKnown: the panel's zone definitions are available (or the panel can't
// provide them), so area 2 can be decided. Before that only area 1 is announced.
void publishDiscovery(bool zonesKnown) {
  const auto& p = *panel;
  String id = nodeId();

  for (int i = 0; i < paradox::kPartitions; i++) {
    String obj = "area" + String(i + 1);
    if (i > 0 && !zonesKnown) continue;
    if (!partitionInUse(i)) {
      publishDiscovery(kDiscArea + i, discTopic("alarm_control_panel", obj), nullptr);
      continue;
    }
    String base = "area/" + String(i + 1);
    JsonDocument d;
    d["name"] = p.partition(i).label;
    d["unique_id"] = id + "_" + obj;
    d["object_id"] = "paradox_" + obj;
    d["state_topic"] = topic((base + "/ha").c_str());
    d["command_topic"] = topic((base + "/set").c_str());
    d["json_attributes_topic"] = topic((base + "/json").c_str());
    d["payload_arm_away"] = "full";
    d["payload_arm_home"] = "stay";
    d["payload_arm_night"] = "sleep";
    d["payload_disarm"] = "disarm";
    JsonArray f = d["supported_features"].to<JsonArray>();
    f.add("arm_home");
    f.add("arm_away");
    f.add("arm_night");
    d["code_arm_required"] = false;
    d["code_disarm_required"] = false;
    addDevice(d);
    publishDiscovery(kDiscArea + i, discTopic("alarm_control_panel", obj), &d);
  }

  // Every enabled zone (local mode + panel definition; unknown definitions count
  // as enabled), even before the panel has answered. Zones set to Never or
  // disabled in the panel are removed.
  for (int z = 0; z < paradox::kZones; z++) {
    String obj = "zone" + String(z + 1);
    if (!zoneEnabled(z)) {
      publishDiscovery(kDiscZone + z, discTopic("binary_sensor", obj), nullptr);
      continue;
    }
    String base = "zone/" + String(z + 1);
    JsonDocument d;
    d["name"] = zoneName(z);
    d["unique_id"] = id + "_" + obj;
    d["object_id"] = "paradox_" + obj;
    d["state_topic"] = topic((base + "/open").c_str());
    d["payload_on"] = "true";
    d["payload_off"] = "false";
    d["device_class"] = zoneSensor(z) == ZoneSensor::Motion ? "motion" : "opening";
    d["json_attributes_topic"] = topic((base + "/json").c_str());
    addDevice(d);
    publishDiscovery(kDiscZone + z, discTopic("binary_sensor", obj), &d);
  }

  {
    JsonDocument d;
    d["name"] = "Panel connection";
    d["unique_id"] = id + "_link";
    d["object_id"] = "paradox_panel_link";
    d["state_topic"] = topic("panel/link");
    d["value_template"] = "{{ 'ON' if value == 'online' else 'OFF' }}";
    d["device_class"] = "connectivity";
    d["entity_category"] = "diagnostic";
    addDevice(d, false);  // must stay available to show "disconnected"
    publishDiscovery(kDiscLink, discTopic("binary_sensor", "link"), &d);
  }
  {
    JsonDocument d;
    d["name"] = "Troubles";
    d["unique_id"] = id + "_troubles";
    d["object_id"] = "paradox_troubles";
    d["state_topic"] = topic("troubles");
    d["value_template"] = "{{ value_json | count }}";
    d["json_attributes_topic"] = topic("troubles");
    d["json_attributes_template"] = "{{ {'troubles': value_json} | tojson }}";
    d["icon"] = "mdi:alert-circle-outline";
    d["entity_category"] = "diagnostic";
    addDevice(d);
    publishDiscovery(kDiscTroubles, discTopic("sensor", "troubles"), &d);
  }
  {
    JsonDocument d;
    d["name"] = "AC power";
    d["unique_id"] = id + "_ac_power";
    d["object_id"] = "paradox_ac_power";
    d["state_topic"] = topic("panel/ac_power");
    d["payload_on"] = "true";   // power detected
    d["payload_off"] = "false"; // AC failure
    d["device_class"] = "power";
    addDevice(d);
    publishDiscovery(kDiscAc, discTopic("binary_sensor", "ac_power"), &d);
  }
  {
    JsonDocument d;
    d["name"] = "Panel battery";
    d["unique_id"] = id + "_battery_low";
    d["object_id"] = "paradox_battery_low";
    d["state_topic"] = topic("panel/battery_low");
    d["payload_on"] = "true";   // low / missing
    d["payload_off"] = "false"; // OK
    d["device_class"] = "battery";
    addDevice(d);
    publishDiscovery(kDiscBattery, discTopic("binary_sensor", "battery_low"), &d);
  }
}

const char* homekitTarget(const paradox::Partition& a) {
  if (!a.armed()) return "D";
  if (a.stayArmed()) return "SA";
  if (a.sleepArmed()) return "NA";
  return "AA";
}

void publishState() {
  const auto& p = *panel;
  publishSlot(kLink, topic("panel/link"), paradox::linkStateName(p.state()));
  publishSlot(kAlarm, topic("panel/alarm"), p.systemAlarm() ? "true" : "false");

  {
    JsonDocument d;
    d["version"] = FW_VERSION;
    d["ip"] = WiFi.localIP().toString();
    d["hostname"] = cfg.hostname;
    d["panelDetected"] = p.info().valid;
    d["model"] = p.info().model();
    char fw[16];
    snprintf(fw, sizeof fw, "%u.%u.%u", p.info().fwVersion, p.info().fwRevision, p.info().fwBuild);
    d["firmware"] = fw;
    String s;
    serializeJson(d, s);
    publishSlot(kInfo, topic("info"), s);
  }

  // Zone / area data is only meaningful once every status page has arrived.
  bool complete = p.state() == paradox::LinkState::Online && (p.system().pagesSeen & 0x3F) == 0x3F;

  // Discovery: area 1 + diagnostics right away; zones (and area 2) once we know
  // which zones exist (panel definitions, cached or fresh).
  static uint32_t lastDiscMs = 0;
  if (cfg.haDiscovery && (millis() - lastDiscMs >= 2000 || lastDiscMs == 0 || lastHash[kDiscLink] == 0)) {
    lastDiscMs = millis();
    publishDiscovery(p.definitionsValid() || (complete && !p.configReading()));
  }

  if (!complete) return;

  {
    const char* t[32];
    int n = listTroubles(p.system(), t, 32);
    JsonDocument d;
    JsonArray arr = d.to<JsonArray>();
    for (int i = 0; i < n; i++) arr.add(t[i]);
    String s;
    serializeJson(d, s);
    publishSlot(kTroubles, topic("troubles"), s);
  }
  // true = mains (AC) power present, false = AC failure trouble
  publishSlot(kAcPower, topic("panel/ac_power"), p.system().acFailure() ? "false" : "true");
  // true = panel battery low / missing
  publishSlot(kBatteryLow, topic("panel/battery_low"), p.system().batteryLow() ? "true" : "false");

  for (int i = 0; i < paradox::kPartitions; i++) {
    if (!partitionInUse(i)) continue;
    const auto& a = p.partition(i);
    int slot = kAreaBase + i * kAreaSlots;
    String base = "area/" + String(i + 1);
    publishSlot(slot, topic((base + "/state").c_str()), areaState(a));
    publishSlot(slot + 1, topic((base + "/homekit").c_str()), a.inAlarm() ? "T" : homekitTarget(a));
    publishSlot(slot + 2, topic((base + "/homekit/target").c_str()), homekitTarget(a));
    publishSlot(slot + 3, topic((base + "/ready").c_str()), a.ready() ? "true" : "false");
    JsonDocument d;
    d["name"] = a.label;
    d["state"] = areaState(a);
    d["armed"] = a.armed();
    d["mode"] = a.modeName();
    d["ready"] = a.ready();
    d["alarm"] = a.inAlarm();
    d["fireAlarm"] = a.fireAlarm();
    d["entryDelay"] = a.entryDelay();
    d["exitDelay"] = a.exitDelay();
    d["alarmMemory"] = a.alarmMemory();
    d["zoneBypassed"] = a.zoneBypassed();
    d["stayD"] = a.stayD();
    String s;
    serializeJson(d, s);
    publishSlot(slot + 4, topic((base + "/json").c_str()), s);
    publishSlot(slot + 5, topic((base + "/ha").c_str()), haState(a));
  }

  for (int z = 0; z < paradox::kZones; z++) {
    String base = "zone/" + String(z + 1);
    int slot = kZoneBase + z * 2;
    if (!zoneEnabled(z)) {
      // Zone was disabled locally: clear its retained topics once.
      if (zoneWasPublished[z]) {
        publish(topic((base + "/open").c_str()), "");
        publish(topic((base + "/json").c_str()), "");
        lastHash[slot] = lastHash[slot + 1] = 0;
        zoneWasPublished[z] = false;
      }
      continue;
    }
    const auto& s = p.zone(z);
    zoneWasPublished[z] = true;
    publishSlot(slot, topic((base + "/open").c_str()), s.open ? "true" : "false");
    JsonDocument d;
    d["name"] = zoneName(z);
    d["open"] = s.open;
    d["alarm"] = s.alarm();
    d["bypassed"] = s.bypassed();
    d["tamper"] = s.tamper;
    d["lowBattery"] = s.lowBattery;
    d["supervision"] = s.rfSupervision;
    d["fireTrouble"] = s.fireTrouble;
    d["signal"] = s.signal;
    d["areas"] = s.partitionMask();
    d["type"] = paradox::zoneDefinitionName(s.definition);
    d["sensor"] = zoneSensorName(zoneSensor(z));
    String js;
    serializeJson(d, js);
    publishSlot(slot + 1, topic((base + "/json").c_str()), js);
  }

  if (strcmp(lastActionSeen, p.lastAction()) != 0) {
    strlcpy(lastActionSeen, p.lastAction(), sizeof lastActionSeen);
    if (lastActionSeen[0]) publish(topic("action/result"), lastActionSeen, false);
  }
}

void result(const char* text) { publish(topic("action/result"), text, false); }

void onMessage(char* t, byte* payload, unsigned int len) {
  String tp(t);
  String msg;
  msg.reserve(len);
  for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  String m = msg;
  m.toLowerCase();

  if (cfg.haDiscovery && tp == cfg.haPrefix + "/status") {
    if (m == "online") {  // Home Assistant restarted: send discovery + states again
      Serial.println("[mqtt] Home Assistant online - republishing discovery");
      memset(lastHash, 0, sizeof lastHash);
    }
    return;
  }

  String prefix = cfg.mqttBase + "/";
  if (!tp.startsWith(prefix)) return;
  tp = tp.substring(prefix.length());
  Serial.printf("[mqtt] command %s = %s\n", tp.c_str(), msg.c_str());

  if (!cfg.mqttAllowControl) return result("control over MQTT is disabled");

  bool ok = false;
  if (tp == "beep") {
    ok = panel->beep();
  } else if (tp.startsWith("area/") && tp.endsWith("/set")) {
    int n = tp.substring(5).toInt();
    if (n < 1 || n > paradox::kPartitions) return result("bad area number");
    uint8_t a = n - 1;
    if (m == "full" || m == "away" || m == "aa" || m == "arm") ok = panel->armFull(a);
    else if (m == "stay" || m == "home" || m == "sa") ok = panel->armStay(a);
    else if (m == "sleep" || m == "night" || m == "na") ok = panel->armSleep(a);
    else if (m == "disarm" || m == "off" || m == "d") ok = panel->disarm(a);
    else return result("unknown area command");
  } else if (tp.startsWith("zone/") && tp.endsWith("/bypass")) {
    int n = tp.substring(5).toInt();
    if (n < 1 || n > paradox::kZones || !zoneEnabled(n - 1)) return result("bad or disabled zone");
    ok = panel->bypass(n - 1);
  } else if (tp.startsWith("pgm/") && tp.endsWith("/set")) {
    int n = tp.substring(4).toInt();
    if (n < 1 || n > paradox::kPgms) return result("bad PGM number");
    if (m == "on" || m == "true" || m == "1") ok = panel->pgmOn(n - 1);
    else if (m == "off" || m == "false" || m == "0") ok = panel->pgmOff(n - 1);
    else return result("PGM command must be on or off");
  } else {
    return;
  }
  if (!ok) result("panel offline or busy");
  // On success the panel's answer is published via action/result by publishState().
}

void tryConnect() {
  client.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
  String id = cfg.hostname + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  String will = topic("status");
  bool ok = cfg.mqttUser.length()
                ? client.connect(id.c_str(), cfg.mqttUser.c_str(), cfg.mqttPassword.c_str(),
                                 will.c_str(), 1, true, "offline")
                : client.connect(id.c_str(), will.c_str(), 1, true, "offline");
  if (!ok) {
    snprintf(status, sizeof status, "connect failed (rc %d)", client.state());
    Serial.printf("[mqtt] %s to %s:%u\n", status, cfg.mqttHost.c_str(), cfg.mqttPort);
    return;
  }
  strlcpy(status, "connected", sizeof status);
  Serial.printf("[mqtt] connected to %s:%u\n", cfg.mqttHost.c_str(), cfg.mqttPort);
  publish(will, "online");
  memset(lastHash, 0, sizeof lastHash);  // republish everything
  for (const char* s : {"area/+/set", "zone/+/bypass", "pgm/+/set", "beep"})
    client.subscribe(topic(s).c_str());
  if (cfg.haDiscovery) client.subscribe((cfg.haPrefix + "/status").c_str());
}

}  // namespace

void mqttBegin(paradox::Panel& p) {
  panel = &p;
  if (!cfg.mqttEnabled) return;
  if (cfg.mqttHost.isEmpty()) {
    strlcpy(status, "no broker configured", sizeof status);
    return;
  }
  client.setBufferSize(1536);  // HA discovery configs are ~700 bytes
  client.setKeepAlive(30);
  client.setSocketTimeout(3);
  client.setCallback(onMessage);
  net.setTimeout(3);
  strlcpy(status, "waiting for WiFi", sizeof status);
}

void mqttLoop() {
  if (!cfg.mqttEnabled || cfg.mqttHost.isEmpty()) return;
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(status, "waiting for WiFi", sizeof status);
    return;
  }
  if (!client.connected()) {
    if (millis() - lastAttemptMs > 5000 || lastAttemptMs == 0) {
      lastAttemptMs = millis();
      strlcpy(status, "connecting", sizeof status);
      tryConnect();
    }
    return;
  }
  client.loop();
  if (millis() - lastScanMs >= 250) {
    lastScanMs = millis();
    publishState();
  }
}

void mqttPublishEvent(const paradox::Event& e) {
  if (!client.connected()) return;
  char text[96], when[20];
  describeEvent(*panel, e, text, sizeof text);
  snprintf(when, sizeof when, "%04u-%02u-%02u %02u:%02u", e.year, e.month, e.day, e.hour, e.minute);
  JsonDocument d;
  d["time"] = when;
  d["group"] = e.group;
  d["sub"] = e.sub;
  d["partition"] = e.partition;
  d["text"] = text;
  if (const char* who = eventUserName(*panel, e)) {
    d["user"] = who;
    d["userNumber"] = e.sub;
  }
  String s;
  serializeJson(d, s);
  publish(topic("event"), s, false);
}

bool mqttRepublish() {
  if (!client.connected()) return false;
  memset(lastHash, 0, sizeof lastHash);  // every slot counts as "changed"
  Serial.println("[mqtt] republishing discovery + states (requested)");
  publishState();
  return true;
}

const char* mqttStatus() { return cfg.mqttEnabled ? status : "disabled"; }
