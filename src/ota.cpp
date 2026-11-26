#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include "display.h"
#include "settings.h"

namespace {
bool started = false;
}

void otaProgress(const char* source, unsigned done, unsigned total) {
  static int lastPct = -1;
  int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
  if (pct == lastPct) return;
  lastPct = pct;
  if (pct % 10 == 0) Serial.printf("[ota] %s %d%%\n", source, pct);
  displayProgress("Firmware update", pct);
}

void otaBegin() {
  if (!cfg.otaNetwork) return;
  ArduinoOTA.setHostname(cfg.hostname.c_str());
  ArduinoOTA.setPassword(cfg.webPassword.c_str());
  ArduinoOTA.setMdnsEnabled(false);  // mDNS is already running (main.cpp)
  ArduinoOTA.onStart([] { Serial.println("[ota] network update started"); });
  ArduinoOTA.onProgress([](unsigned done, unsigned total) { otaProgress("network", done, total); });
  ArduinoOTA.onEnd([] {
    Serial.println("[ota] done, rebooting");
    displayMessage("Update done - rebooting");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", e);
    displayMessage("Update FAILED");
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);  // so PlatformIO / the Arduino IDE can find it
  started = true;
}

void otaLoop() {
  if (started) ArduinoOTA.handle();
}
