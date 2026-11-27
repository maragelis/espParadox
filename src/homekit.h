#pragma once
// Apple HomeKit (HomeSpan): the bridge appears in the Home app as a bridge with
//   - one Security System per area in use   (Stay = Home, Full = Away, Sleep = Night)
//   - one Contact or Motion sensor per enabled zone (type from Settings -> Zones),
//     with Tampered and Low battery.
//   - "AC power" and "Panel battery" contact sensors (closed = OK, open = trouble);
//     the area alarms show Fault while either has a trouble.
// HomeKit and MQTT are mutually exclusive (Settings -> Smart home).
// The accessory list is built at boot: after changing zones, reboot to update it.

#include <Arduino.h>

#include "paradox.h"

constexpr uint16_t kHomekitPort = 1201;  // HAP port (the web UI keeps port 80)

struct HomekitInfo {
  bool enabled = false;   // chosen in Settings
  bool started = false;   // HomeSpan running
  bool paired = false;    // at least one Home app / iPhone paired
  int controllers = 0;    // paired controllers (users / hubs)
  int accessories = 0;    // areas + zones exposed
  String code;            // setup code "123-45-678"
  String setupUri;        // X-HM://... (QR payload)
  String host;            // hostname.local
  uint16_t port = kHomekitPort;
  String status;          // human readable
};

void homekitBegin(paradox::Panel& panel);
void homekitLoop();
HomekitInfo homekitInfo();

// Remove all pairings (the Home app must add the bridge again). Returns false if not running.
bool homekitUnpair();
// Generate a new random setup code, save it and apply it immediately.
bool homekitNewCode();

bool homekitCodeValid(const String& code);  // 8 digits, not a trivial code
String homekitRandomCode();

// SVG image of the setup QR code (scan with the iPhone camera / Home app). Empty if off.
String homekitQrSvg();
