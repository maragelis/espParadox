#include "homekit.h"

#include <HomeSpan.h>
#include <sodium.h>  // HAP.h (controller list) needs the libsodium sizes first, like HomeSpan's own sources
#include <HAP.h>
#include <Preferences.h>
#include <esp_system.h>

#include "net.h"
#include "qrgen.h"
#include "settings.h"
#include "version.h"
#include "zones.h"

namespace {

paradox::Panel* panel = nullptr;
bool started = false;
int nAccessories = 0;
uint32_t lastHomekitWriteMs = 0;  // a Home app change is pending on the panel

constexpr const char* kSetupId = "PDXB";  // 4-char setup ID used in the QR code / mDNS
constexpr uint8_t kCategoryBridge = 2;    // HAP category "Bridges"

bool statusReady() {
  return panel->state() == paradox::LinkState::Online && (panel->system().pagesSeen & 0x06) == 0x06;
}

// HomeKit Security System states: 0 Stay(Home) 1 Away 2 Night 3 Disarmed 4 Triggered
uint8_t targetFor(const paradox::Partition& a) {
  if (!a.armed()) return 3;
  if (a.stayArmed()) return 0;
  if (a.sleepArmed()) return 2;
  return 1;
}

uint8_t currentFor(const paradox::Partition& a) { return a.inAlarm() ? 4 : targetFor(a); }

// Status page 0 (troubles: AC failure, no / low battery) has arrived.
bool troublesReady() {
  return panel->state() == paradox::LinkState::Online && (panel->system().pagesSeen & 0x01);
}

void accessoryInfo(const char* name, const String& serial) {
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name(name);
  new Characteristic::Manufacturer("Paradox");
  new Characteristic::Model("SP/MG serial bridge");
  new Characteristic::SerialNumber(serial.c_str());
  new Characteristic::FirmwareRevision(FW_VERSION);
}

// ---- Security System per area -------------------------------------------------------

struct AreaService : Service::SecuritySystem {
  int area;
  SpanCharacteristic* current;
  SpanCharacteristic* target;
  SpanCharacteristic* fault;
  uint32_t lastCheck = 0;

  explicit AreaService(int a) : area(a) {
    current = new Characteristic::SecuritySystemCurrentState(3);
    target = new Characteristic::SecuritySystemTargetState(3);
    fault = new Characteristic::StatusFault(0);  // 1 while AC power or the panel battery has a trouble
  }

  // Home app picked Home / Away / Night / Off.
  boolean update() override {
    if (panel->state() != paradox::LinkState::Online) return false;  // shows "No response"
    int t = target->getNewVal();
    bool ok = t == 0 ? panel->armStay(area)
              : t == 1 ? panel->armFull(area)
              : t == 2 ? panel->armSleep(area)
                       : panel->disarm(area);
    if (ok) lastHomekitWriteMs = millis();
    return ok;
  }

  void loop() override {
    if (millis() - lastCheck < 500) return;
    lastCheck = millis();
    if (!statusReady()) return;
    const auto& a = panel->partition(area);
    uint8_t c = currentFor(a);
    if (current->getVal() != c) current->setVal(c);
    // Follow changes made elsewhere (keypad, web, ...). After a Home app change, give the
    // panel 20 s to act (exit delay, refusal) before putting the target back in line.
    uint8_t t = targetFor(a);
    if (target->getVal() != t && millis() - lastHomekitWriteMs > 20000) target->setVal(t);
    if (troublesReady()) {
      const auto& s = panel->system();
      uint8_t f = (s.acFailure() || s.batteryLow()) ? 1 : 0;
      if (fault->getVal() != f) fault->setVal(f);
    }
  }
};

// ---- Panel AC power / battery (HomeKit has no mains-power sensor: contact sensors) ----
// Closed = OK, open = trouble. The battery sensor also sets Low battery.

struct PanelTroubleSensor : Service::ContactSensor {
  bool battery;
  SpanCharacteristic* state;
  SpanCharacteristic* low = nullptr;
  uint32_t lastCheck = 0;
  explicit PanelTroubleSensor(bool isBattery) : battery(isBattery) {
    state = new Characteristic::ContactSensorState(0);
    if (battery) low = new Characteristic::StatusLowBattery(0);
  }
  void loop() override {
    if (millis() - lastCheck < 1000) return;
    lastCheck = millis();
    if (!troublesReady()) return;
    bool trouble = battery ? panel->system().batteryLow() : panel->system().acFailure();
    if (state->getVal() != (trouble ? 1 : 0)) state->setVal(trouble ? 1 : 0);
    if (low && low->getVal() != (trouble ? 1 : 0)) low->setVal(trouble ? 1 : 0);
  }
};

// ---- Zone sensors ---------------------------------------------------------------------

struct ZoneContact : Service::ContactSensor {
  int zone;
  SpanCharacteristic *state, *tamper, *battery;
  uint32_t lastCheck = 0;
  explicit ZoneContact(int z) : zone(z) {
    state = new Characteristic::ContactSensorState(0);  // 0 = closed (contact detected), 1 = open
    tamper = new Characteristic::StatusTampered(0);
    battery = new Characteristic::StatusLowBattery(0);
  }
  void loop() override {
    if (millis() - lastCheck < 500) return;
    lastCheck = millis();
    if (!statusReady()) return;
    const auto& z = panel->zone(zone);
    if (state->getVal() != (z.open ? 1 : 0)) state->setVal(z.open ? 1 : 0);
    if (tamper->getVal() != (z.tamper ? 1 : 0)) tamper->setVal(z.tamper ? 1 : 0);
    if (battery->getVal() != (z.lowBattery ? 1 : 0)) battery->setVal(z.lowBattery ? 1 : 0);
  }
};

struct ZoneMotion : Service::MotionSensor {
  int zone;
  SpanCharacteristic *motion, *tamper, *battery;
  uint32_t lastCheck = 0;
  explicit ZoneMotion(int z) : zone(z) {
    motion = new Characteristic::MotionDetected(false);
    tamper = new Characteristic::StatusTampered(0);
    battery = new Characteristic::StatusLowBattery(0);
  }
  void loop() override {
    if (millis() - lastCheck < 500) return;
    lastCheck = millis();
    if (!statusReady()) return;
    const auto& z = panel->zone(zone);
    if (motion->getVal() != (z.open ? 1 : 0)) motion->setVal(z.open);
    if (tamper->getVal() != (z.tamper ? 1 : 0)) tamper->setVal(z.tamper ? 1 : 0);
    if (battery->getVal() != (z.lowBattery ? 1 : 0)) battery->setVal(z.lowBattery ? 1 : 0);
  }
};

int controllerCount() {
  if (!started) return 0;
  return (int)std::distance(homeSpan.controllerListBegin(), homeSpan.controllerListEnd());
}

String formatCode(const String& c) { return c.length() == 8 ? c.substring(0, 3) + "-" + c.substring(3, 5) + "-" + c.substring(5) : c; }

String setupUri() {
  HapQR qr;
  return String(qr.get((uint32_t)strtoul(cfg.homekitCode.c_str(), nullptr, 10), kSetupId, kCategoryBridge));
}

// Apply the setup code to HomeSpan only when it changed (computing it takes ~1 s).
void applyCode(bool force) {
  Preferences p;
  p.begin("homekit", false);
  if (force || p.getString("applied", "") != cfg.homekitCode) {
    homeSpan.setPairingCode(cfg.homekitCode.c_str(), false);
    p.putString("applied", cfg.homekitCode);
    Serial.println("[homekit] setup code applied");
  }
  p.end();
}

// ---- cleanup when HomeKit is off --------------------------------------------------------

// True if NVS namespace `ns` holds any key other than `keep`.
bool nvsHasData(const char* ns, const char* keep) {
  bool found = false;
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY);
  while (it) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (!keep || strcmp(info.key, keep) != 0) found = true;
    it = nvs_entry_next(it);
  }
  nvs_release_iterator(it);
  return found;
}

bool nvsErase(const char* ns, const char* onlyKey) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = onlyKey ? nvs_erase_key(h, onlyKey) : nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

// HomeKit is not selected: remove everything HomeSpan stored (WiFi credentials copy,
// accessory identity + keys, paired controllers, setup-code verifier, characteristic
// values) and the bridge's own setup code. Only writes when something is there.
// HomeSpan's reboot counter ("WIFI"/"REBOOTS") is left alone: the library's global
// object rewrites it at every start, so erasing it would only cause extra writes.
void cleanup() {
  struct Area { const char* ns; const char* keep; };
  const Area areas[] = {{"HAP", nullptr}, {"SRP", nullptr}, {"CHAR", nullptr}, {"OTA", nullptr},
                        {"POINT", nullptr}, {"WIFI", "REBOOTS"}};
  int erased = 0;
  for (const auto& a : areas) {
    if (!nvsHasData(a.ns, a.keep)) continue;
    bool ok = a.keep ? nvsErase(a.ns, "WIFIDATA") : nvsErase(a.ns, nullptr);
    if (ok) erased++;
  }
  Preferences p;
  if (p.begin("homekit", true)) {
    bool had = p.isKey("applied");
    p.end();
    if (had && p.begin("homekit", false)) {
      p.clear();
      p.end();
      erased++;
    }
  }
  if (cfg.homekitCode.length()) {  // a new code is made if HomeKit is turned on again
    cfg.homekitCode = "";
    settingsSave();
    erased++;
  }
  if (erased) Serial.println("[homekit] off - removed HomeKit pairing data, keys, setup code and WiFi copy");
}

}  // namespace

bool homekitCodeValid(const String& c) {
  if (c.length() != 8) return false;
  for (char ch : c)
    if (ch < '0' || ch > '9') return false;
  bool same = true;
  for (char ch : c) same &= ch == c[0];
  return !same && c != "12345678" && c != "87654321";
}

String homekitRandomCode() {
  char buf[9];
  do {
    snprintf(buf, sizeof buf, "%08lu", (unsigned long)(esp_random() % 100000000UL));
  } while (!homekitCodeValid(buf));
  return buf;
}

void homekitBegin(paradox::Panel& p) {
  panel = &p;
  if (!cfg.homekitEnabled) {
    cleanup();  // switched to MQTT / nothing: don't leave HomeSpan data behind
    return;
  }
  if (cfg.wifiSsid.isEmpty()) {
    Serial.println("[homekit] needs a WiFi network (Settings -> Network) - not started");
    return;
  }
  if (!homekitCodeValid(cfg.homekitCode)) {
    cfg.homekitCode = homekitRandomCode();
    settingsSave();
  }

  homeSpan.setLogLevel(-1);               // keep the USB console for the bridge
  homeSpan.setSerialInputDisable(true);   // HomeSpan's own serial CLI off
  homeSpan.setPortNum(kHomekitPort);      // web UI stays on port 80
  homeSpan.setHostNameSuffix("");         // same mDNS name as the bridge (paradox-84.local)
  homeSpan.setQRID(kSetupId);
  homeSpan.setSketchVersion(FW_VERSION);
  homeSpan.setPairCallback([](boolean isPaired) {
    Serial.printf("[homekit] %s\n", isPaired ? "paired" : "unpaired");
  });
  // HomeSpan only starts HomeKit (mDNS + pairing server) when it has the credentials;
  // while HomeKit is on it also does the WiFi reconnects (see netLoop()).
  homeSpan.setWifiCredentials(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
  homeSpan.begin(Category::Bridges, "Paradox Alarm", netHostname().c_str(), "Paradox SP/MG Bridge");
  applyCode(false);

  // Fixed accessory IDs so rooms / names in the Home app survive reboots and changes.
  new SpanAccessory(1);
  accessoryInfo("Paradox Alarm", cfg.hostname);
  nAccessories = 0;
  new SpanAccessory(2);
  accessoryInfo("AC power", "panel-ac");
  new PanelTroubleSensor(false);
  new SpanAccessory(3);
  accessoryInfo("Panel battery", "panel-battery");
  new PanelTroubleSensor(true);
  nAccessories += 2;
  for (int i = 0; i < paradox::kPartitions; i++) {
    if (!partitionInUse(i)) continue;
    new SpanAccessory(100 + i + 1);
    accessoryInfo(p.partition(i).label, "area-" + String(i + 1));
    new AreaService(i);
    nAccessories++;
  }
  for (int z = 0; z < paradox::kZones; z++) {
    if (!zoneEnabled(z)) continue;
    new SpanAccessory(200 + z + 1);
    accessoryInfo(zoneName(z), "zone-" + String(z + 1));
    if (zoneSensor(z) == ZoneSensor::Motion) new ZoneMotion(z);
    else new ZoneContact(z);
    nAccessories++;
  }
  started = true;
  Serial.printf("[homekit] started: %d accessories, setup code %s, port %u\n", nAccessories,
                formatCode(cfg.homekitCode).c_str(), kHomekitPort);
}

void homekitLoop() {
  if (started) homeSpan.poll();
}

HomekitInfo homekitInfo() {
  HomekitInfo i;
  i.enabled = cfg.homekitEnabled;
  i.started = started;
  i.controllers = controllerCount();
  i.paired = i.controllers > 0;
  i.accessories = nAccessories;
  i.host = netHostname() + ".local";
  if (i.enabled && homekitCodeValid(cfg.homekitCode)) {
    i.code = formatCode(cfg.homekitCode);
    i.setupUri = setupUri();
  }
  if (!i.enabled) i.status = "off";
  else if (!started) i.status = cfg.wifiSsid.isEmpty() ? "needs a WiFi network" : "starting after reboot";
  else if (WiFi.status() != WL_CONNECTED) i.status = "waiting for WiFi";
  else if (!i.paired) i.status = "ready to pair";
  else i.status = "paired";
  return i;
}

bool homekitUnpair() {
  if (!started) return false;
  homeSpan.processSerialCommand("U");  // deletes all controller pairings, drops connections
  Serial.println("[homekit] all pairings removed");
  return true;
}

bool homekitNewCode() {
  cfg.homekitCode = homekitRandomCode();
  settingsSave();
  if (started) applyCode(true);
  return true;
}

String homekitQrSvg() {
  HomekitInfo info = homekitInfo();
  if (info.setupUri.isEmpty()) return String();
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(3)];
  if (qrcode_initText(&qr, buf, 3, ECC_MEDIUM, info.setupUri.c_str()) != 0) return String();
  const int q = 4;  // quiet zone
  int n = qr.size + 2 * q;
  String svg;
  svg.reserve(6000);
  svg += "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " + String(n) + " " + String(n) +
         "' shape-rendering='crispEdges'><rect width='100%' height='100%' fill='#fff'/><path fill='#000' d='";
  for (int y = 0; y < qr.size; y++)
    for (int x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) svg += "M" + String(x + q) + " " + String(y + q) + "h1v1h-1z";
  svg += "'/></svg>";
  return svg;
}
