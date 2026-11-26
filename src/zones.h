#pragma once
// Local (board-only) zone settings and the cached copy of the panel's
// configuration. Nothing here is ever written to the panel.
//
// Each zone has:
//   - a custom name (empty = use the label programmed in the panel)
//   - a mode: Auto (follow the panel: hidden if the panel has it disabled),
//             Enabled (always shown) or Disabled (always hidden)

#include <Arduino.h>

#include "paradox.h"

enum class ZoneMode : uint8_t { Auto = 0, Enabled = 1, Disabled = 2 };
const char* zoneModeName(ZoneMode m);

// How the zone is presented to Home Assistant / Homebridge / Homey.
enum class ZoneSensor : uint8_t { Contact = 0, Motion = 1 };
const char* zoneSensorName(ZoneSensor s);  // "contact" / "motion"

constexpr size_t kCustomLabelLen = 32;

// Loads local settings + cached panel config, applies the cache to the panel,
// and hooks the panel so a fresh configuration read updates the cache.
void zonesBegin(paradox::Panel& panel);

const char* zoneName(int zone);           // 0-based; custom name or panel label
const char* zoneCustomLabel(int zone);
ZoneMode zoneMode(int zone);
ZoneSensor zoneSensor(int zone);
bool zoneEnabled(int zone);               // effective: mode + panel definition
bool zoneSet(int zone, const String& customLabel, ZoneMode mode, ZoneSensor sensor);  // saves to flash
// Bumped whenever a zone's name / mode / sensor type changes (MQTT discovery refresh).
uint32_t zonesRevision();
void zonesResetLocal();                   // clear all custom names and modes

// A partition is in use if any enabled zone is assigned to it (area 1 always is).
bool partitionInUse(int partition);

bool panelConfigCached();                 // a snapshot from the panel is stored
void panelConfigForget();                 // erase the cached snapshot
