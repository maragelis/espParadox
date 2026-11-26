#include "zones.h"

#include <Preferences.h>

namespace {

paradox::Panel* panel = nullptr;
char customLabel[paradox::kZones][kCustomLabelLen + 1];
ZoneMode mode[paradox::kZones];
ZoneSensor sensor[paradox::kZones];
uint32_t revision = 1;
bool cached = false;

constexpr const char* kZonesNs = "zones";
constexpr const char* kPanelNs = "panelcfg";

void saveSnapshot(const paradox::PanelConfig& c) {
  Preferences p;
  p.begin(kPanelNs, false);
  // Only write when it changed, to spare the flash.
  paradox::PanelConfig old;
  bool same = p.getBytesLength("cfg") == sizeof old && p.getBytes("cfg", &old, sizeof old) == sizeof old &&
              memcmp(&old, &c, sizeof c) == 0;
  if (!same) {
    p.putBytes("cfg", &c, sizeof c);
    Serial.println("[zones] panel configuration cached");
  }
  p.end();
  cached = true;
}

}  // namespace

const char* zoneModeName(ZoneMode m) {
  switch (m) {
    case ZoneMode::Enabled: return "enabled";
    case ZoneMode::Disabled: return "disabled";
    default: return "auto";
  }
}

void zonesBegin(paradox::Panel& p) {
  panel = &p;

  Preferences prefs;
  prefs.begin(kZonesNs, true);
  for (int z = 0; z < paradox::kZones; z++) {
    char key[8];
    snprintf(key, sizeof key, "l%d", z);
    String l = prefs.getString(key, "");
    strlcpy(customLabel[z], l.c_str(), sizeof customLabel[z]);
    snprintf(key, sizeof key, "m%d", z);
    uint8_t m = prefs.getUChar(key, 0);
    mode[z] = m <= 2 ? (ZoneMode)m : ZoneMode::Auto;
    snprintf(key, sizeof key, "t%d", z);
    sensor[z] = prefs.getUChar(key, 0) == 1 ? ZoneSensor::Motion : ZoneSensor::Contact;
  }
  prefs.end();

  prefs.begin(kPanelNs, true);
  paradox::PanelConfig c;
  if (prefs.getBytesLength("cfg") == sizeof c && prefs.getBytes("cfg", &c, sizeof c) == sizeof c &&
      c.magic == paradox::PanelConfig::kMagic) {
    panel->restore(c);
    cached = true;
    Serial.println("[zones] restored cached panel configuration");
  }
  prefs.end();

  panel->onConfigRead = saveSnapshot;
}

const char* zoneName(int z) {
  if (z < 0 || z >= paradox::kZones) return "?";
  return customLabel[z][0] ? customLabel[z] : panel->zone(z).label;
}

const char* zoneCustomLabel(int z) { return customLabel[z]; }
ZoneMode zoneMode(int z) { return mode[z]; }
ZoneSensor zoneSensor(int z) { return sensor[z]; }
uint32_t zonesRevision() { return revision; }

const char* zoneSensorName(ZoneSensor s) { return s == ZoneSensor::Motion ? "motion" : "contact"; }

bool zoneEnabled(int z) {
  switch (mode[z]) {
    case ZoneMode::Enabled: return true;
    case ZoneMode::Disabled: return false;
    default: return panel->zone(z).panelEnabled();
  }
}

bool zoneSet(int z, const String& label, ZoneMode m, ZoneSensor s) {
  if (z < 0 || z >= paradox::kZones || label.length() > kCustomLabelLen) return false;
  strlcpy(customLabel[z], label.c_str(), sizeof customLabel[z]);
  mode[z] = m;
  sensor[z] = s;
  revision++;
  Preferences prefs;
  prefs.begin(kZonesNs, false);
  char key[8];
  snprintf(key, sizeof key, "l%d", z);
  if (label.length()) prefs.putString(key, label);
  else prefs.remove(key);
  snprintf(key, sizeof key, "m%d", z);
  prefs.putUChar(key, (uint8_t)m);
  snprintf(key, sizeof key, "t%d", z);
  prefs.putUChar(key, (uint8_t)s);
  prefs.end();
  return true;
}

void zonesResetLocal() {
  Preferences prefs;
  prefs.begin(kZonesNs, false);
  prefs.clear();
  prefs.end();
  for (int z = 0; z < paradox::kZones; z++) {
    customLabel[z][0] = '\0';
    mode[z] = ZoneMode::Auto;
    sensor[z] = ZoneSensor::Contact;
  }
  revision++;
}

bool partitionInUse(int part) {
  if (part == 0) return true;
  for (int z = 0; z < paradox::kZones; z++)
    if (zoneEnabled(z) && (panel->zone(z).partitionMask() & (1 << part))) return true;
  return false;
}

bool panelConfigCached() { return cached; }

void panelConfigForget() {
  Preferences prefs;
  prefs.begin(kPanelNs, false);
  prefs.clear();
  prefs.end();
  cached = false;
}
