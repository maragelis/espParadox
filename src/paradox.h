#pragma once
// Paradox MG/SP serial protocol driver (SPXXXX Serial Port Protocol V2.10).
//
// Every message is 37 bytes; byte 36 is a simple checksum (sum of bytes 0-35)
// on a direct connection. The PC side starts a session with 0x5F, initializes
// it with 0x00, and then exchanges 0x30/0x40/0x50/... commands. The panel
// also sends unsolicited 0xE0 live-event messages at any time.

#include <Arduino.h>
#include <functional>

namespace paradox {

constexpr size_t kMsgLen = 37;
constexpr int kZones = 32;
constexpr int kPgms = 16;
constexpr int kPartitions = 2;
constexpr int kUsers = 32;
constexpr int kMaxEvents = 40;
constexpr size_t kLabelLen = 16;

enum class LinkState : uint8_t {
  Disconnected,  // waiting to (re)try a session
  Starting,      // sent 0x5F, waiting for the panel answer
  Initializing,  // sent 0x00, waiting for 0x10
  Online,        // logged in, polling status
};
const char* linkStateName(LinkState s);

struct PanelInfo {
  bool valid = false;
  uint8_t productId = 0;
  uint8_t fwVersion = 0, fwRevision = 0, fwBuild = 0;
  uint8_t panelId[2] = {0, 0};
  const char* model() const;
  // Product IDs listed in the protocol document. Zone definitions are only read
  // for these - the EEPROM map isn't valid on others (e.g. 0x1A, fw 5.x).
  bool knownModel() const { return productIdKnown(productId); }
  static bool productIdKnown(uint8_t id) {
    return id == 0x15 || id == 0x16 || id == 0x17 || id == 0x40 || id == 0x41;
  }
};

// Zone definition as programmed in the panel (EEPROM 0x730 + 3*zone, read only).
// Layout from the open-source PAI project: definition, partition, options.
constexpr uint8_t kDefUnknown = 0xFF;  // definition not read (yet)
const char* zoneDefinitionName(uint8_t def);
const char* zoneAlarmTypeName(uint8_t type);

struct Zone {
  char label[kLabelLen + 1];
  uint8_t definition = kDefUnknown;  // 0 = disabled in the panel
  uint8_t partitionRaw = 0;          // raw partition byte from the panel
  uint8_t options = 0;               // raw options byte from the panel

  bool definitionKnown() const { return definition != kDefUnknown; }
  // Enabled in the panel? Unknown definitions count as enabled.
  bool panelEnabled() const { return definition != 0; }
  // Partition membership (bit 0 = area 1, bit 1 = area 2); defaults to area 1.
  uint8_t partitionMask() const { return (partitionRaw & 0x03) ? (partitionRaw & 0x03) : 0x01; }
  bool optAutoShutdown() const { return options & 0x80; }
  bool optBypassable() const { return options & 0x40; }
  bool optRfSupervision() const { return options & 0x20; }
  uint8_t optAlarmType() const { return (options >> 3) & 0x03; }
  bool optIntellizone() const { return options & 0x04; }
  bool optDelayTransmission() const { return options & 0x02; }
  bool optForceArm() const { return options & 0x01; }

  uint8_t status = 0;  // status page 2 byte (bit layout in the protocol doc)
  bool open = false, tamper = false, fireTrouble = false;
  bool rfSupervision = false, lowBattery = false, exitDelay = false;
  uint8_t signal = 0;  // 1..10, 0 = not reported (wired zone)

  bool wasAlarm() const { return status & 0x80; }
  bool alarm() const { return status & 0x40; }
  uint8_t delayMode() const { return (status >> 4) & 0x03; }  // see delayName()
  bool bypassed() const { return status & 0x08; }
  bool shutdown() const { return status & 0x04; }
  bool txDelay() const { return status & 0x02; }
  bool wasBypassed() const { return status & 0x01; }
  const char* delayName() const;
};

struct Pgm {
  char label[kLabelLen + 1];
  bool tamper = false, rfSupervision = false;
  uint8_t signal = 0;
};

struct Partition {
  char label[kLabelLen + 1];
  uint8_t raw[4] = {0, 0, 0, 0};  // status page 1 bytes 17-20 / 21-24

  bool armed() const { return raw[0] & 0x01; }
  bool sleepArmed() const { return raw[0] & 0x02; }
  bool stayArmed() const { return raw[0] & 0x04; }
  bool strobe() const { return raw[0] & 0x10; }
  bool silentAlarm() const { return raw[0] & 0x20; }
  bool audibleAlarm() const { return raw[0] & 0x40; }
  bool fireAlarm() const { return raw[0] & 0x80; }
  bool inAlarm() const { return raw[0] & 0xE0; }
  bool exitDelay() const { return raw[1] & 0x01; }
  bool entryDelay() const { return raw[1] & 0x02; }
  bool alarmMemory() const { return raw[1] & 0x04; }
  bool zoneBypassed() const { return raw[1] & 0x08; }
  bool bellOn() const { return raw[1] & 0x80; }
  bool ready() const { return raw[3] & 0x01; }
  bool forceArmed() const { return raw[3] & 0x02; }
  bool stayD() const { return raw[3] & 0x04; }
  const char* modeName() const;  // "disarmed" / "stay" / "sleep" / "full"
};

struct SystemStatus {
  uint8_t trouble[5] = {0};  // status page 0 bytes 4-8
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0;
  uint8_t acLevel = 0, dcLevel = 0, batteryLevel = 0, rfNoise = 0;  // raw 0-255
  uint16_t moduleTamper = 0, moduleSupervision = 0;  // bus modules 1-16 (bit 0 = 1)
  uint8_t repeaterKeypadSupervision = 0;
  uint8_t repeaterSignal[2] = {0, 0};
  uint8_t keypadSignal[8] = {0};
  uint8_t pagesSeen = 0;  // bit n set once status page n has been received
  bool acFailure() const { return trouble[2] & 0x02; }  // page 0 byte 6 bit 1
};

// Snapshot of everything read from the panel's programming, so it can be
// cached in flash and restored before the panel answers.
struct __attribute__((packed)) PanelConfig {  // packed: no padding, memcmp-safe
  static constexpr uint32_t kMagic = 0x50434632;  // "PCF2" (adds user labels)
  uint32_t magic = kMagic;
  uint8_t productId = 0;
  bool labelsValid = false;
  bool definitionsValid = false;
  uint8_t zoneDef[kZones][3] = {};
  char zoneLabel[kZones][kLabelLen + 1] = {};
  char pgmLabel[kPgms][kLabelLen + 1] = {};
  char partitionLabel[kPartitions][kLabelLen + 1] = {};
  char userLabel[kUsers][kLabelLen + 1] = {};
};

struct Event {
  uint32_t uptimeMs;
  uint16_t year;
  uint8_t month, day, hour, minute;
  uint8_t group, sub, partition;
  uint8_t serial[4];
};

class Panel {
 public:
  void begin(HardwareSerial& port);
  void loop();

  // --- Control (0x40 Perform Action). Partition 0/1, zone 0-31, pgm 0-15.
  // Return false if the panel is not online or the queue is full.
  bool armStay(uint8_t partition) { return action(0x01, partition, "Stay arm"); }
  bool armSleep(uint8_t partition) { return action(0x03, partition, "Sleep arm"); }
  bool armFull(uint8_t partition) { return action(0x04, partition, "Full arm"); }
  bool disarm(uint8_t partition) { return action(0x05, partition, "Disarm"); }
  bool armStayD(uint8_t partition) { return action(0x06, partition, "Stay arm + StayD"); }
  bool armSleepD(uint8_t partition) { return action(0x07, partition, "Sleep arm + StayD"); }
  bool disarmAllStayDOff() { return action(0x08, 0, "Disarm both + StayD off"); }
  bool bypass(uint8_t zone) { return zone < kZones && action(0x10, zone, "Bypass zone"); }
  bool beep(uint8_t type = 0x04) { return action(0x20, type, "Beep"); }
  bool pgmOn(uint8_t pgm) { return pgm < kPgms && action(0x32, pgm, "PGM on"); }
  bool pgmOff(uint8_t pgm) { return pgm < kPgms && action(0x33, pgm, "PGM off"); }
  // 0x30 Set Time & Date
  bool setTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

  // --- Session control. disconnect() closes the session (0x70) and stops
  // retrying until connect() is called. Not persisted: a reboot reconnects.
  void disconnect();
  void connect();
  bool stopped() const { return stopped_; }

  // --- State
  LinkState state() const { return state_; }
  const char* lastError() const { return lastError_; }
  const char* lastAction() const { return lastAction_; }
  bool systemAlarm() const { return systemAlarm_; }
  // Panel programming (read only - nothing is ever written to the panel EEPROM).
  bool configReading() const { return configBlock_ < kConfigBlocks; }
  bool labelsValid() const { return labelsValid_; }
  bool definitionsValid() const { return definitionsValid_; }
  bool rereadConfig();  // re-read labels + definitions now (if online)
  PanelConfig snapshot() const;
  void restore(const PanelConfig& c);  // apply a cached snapshot
  // Called after a full configuration read with the fresh snapshot.
  std::function<void(const PanelConfig&)> onConfigRead;
  const PanelInfo& info() const { return info_; }
  const SystemStatus& system() const { return sys_; }
  const Zone& zone(int i) const { return zones_[i]; }
  const Pgm& pgm(int i) const { return pgms_[i]; }
  const Partition& partition(int i) const { return partitions_[i]; }
  // User label (0-based user index), e.g. "Maria"; default "User 01".
  const char* userLabel(int i) const { return (i >= 0 && i < kUsers) ? users_[i] : "?"; }
  uint32_t framesRx() const { return framesRx_; }
  uint32_t framesTx() const { return framesTx_; }
  uint32_t checksumErrors() const { return checksumErrors_; }
  uint32_t lastRxMs() const { return lastRxMs_; }
  // Frame counters cover the current window (reset every 10 minutes).
  static constexpr uint32_t kCounterWindowMs = 10UL * 60 * 1000;
  uint32_t countersAgeSec() const { return (millis() - countersResetMs_) / 1000; }

  // Event log, index 0 = newest.
  int eventCount() const { return eventCount_; }
  const Event& event(int i) const;

  std::function<void(const Event&)> onEvent;
  bool rawDump = false;  // hex-dump every frame to USB serial

 private:
  enum class Pending : uint8_t { None, Start, Init, Status, Eeprom, Action, SetTime };
  struct Queued {
    uint8_t frame[kMsgLen];
    Pending kind;
    char desc[32];
  };
  static constexpr int kQueueLen = 8;
  // EEPROM read plan, 32 bytes per read: 16 zone-label blocks, 8 PGM-label
  // blocks, 1 partition-label block, 3 zone-definition blocks (96 bytes).
  static constexpr int kZoneLabelBlocks = 16, kPgmLabelBlocks = 8, kPartLabelBlocks = 1;
  static constexpr int kUserLabelBlocks = 16;  // 32 users, 2 labels per read
  static constexpr int kDefBlocks = 3;
  static constexpr int kFirstUserBlock = kZoneLabelBlocks + kPgmLabelBlocks + kPartLabelBlocks;
  static constexpr int kFirstDefBlock = kFirstUserBlock + kUserLabelBlocks;
  static constexpr int kConfigBlocks = kFirstDefBlock + kDefBlocks;
  static uint16_t configBlockAddr(int block);
  void finishConfigRead();

  bool action(uint8_t code, uint8_t arg, const char* desc);
  bool enqueue(const uint8_t* frame, Pending kind, const char* desc);
  void prepare(uint8_t* m) const;  // fill source/user ID + checksum
  void send(const uint8_t* m, Pending kind);
  void readSerial();
  void handleFrame(const uint8_t* m);
  void handleStatus(const uint8_t* m);
  void handleEeprom(const uint8_t* m);
  void handleEvent(const uint8_t* m);
  void addEvent(const Event& e);
  void acTransition(bool failed);
  void startSession();
  void sendInit();
  void sendNext();
  void onTimeout();
  void dropLink(const char* why, uint32_t retryMs);
  void dump(const char* dir, const uint8_t* m);

  HardwareSerial* port_ = nullptr;
  LinkState state_ = LinkState::Disconnected;
  Pending pending_ = Pending::None;
  uint8_t pendingFrame_[kMsgLen];
  char pendingDesc_[32] = "";
  uint32_t sentAtMs_ = 0, lastReplyMs_ = 0, nextAttemptMs_ = 0, lastRxMs_ = 0;
  uint8_t timeouts_ = 0;
  char lastError_[64] = "";
  char lastAction_[64] = "";
  bool systemAlarm_ = false;
  bool stopped_ = false;  // disconnected by the user
  // AC failure tracking: the bridge logs its own event when the status bit changes,
  // unless the panel sent its own live event (group 44/45 sub 1) around the same time.
  bool acKnown_ = false, acFailed_ = false;
  uint32_t acRealEventMs_ = 0, acOwnEventMs_ = 0;
  uint8_t acRealGroup_ = 0, acOwnGroup_ = 0;

  uint8_t rx_[kMsgLen];
  size_t rxLen_ = 0;
  uint32_t rxByteMs_ = 0;

  Queued queue_[kQueueLen];
  int qHead_ = 0, qCount_ = 0;

  uint8_t statusPage_ = 0;
  int configBlock_ = kConfigBlocks;
  bool configBlockFailed_ = false;
  uint8_t configRetries_ = 0;  // retries of the current config block
  bool labelsValid_ = false, definitionsValid_ = false;
  uint8_t defBuf_[kDefBlocks * 32];

  PanelInfo info_;
  SystemStatus sys_;
  Zone zones_[kZones];
  Pgm pgms_[kPgms];
  Partition partitions_[kPartitions];
  char users_[kUsers][kLabelLen + 1];

  Event events_[kMaxEvents];
  int eventHead_ = 0, eventCount_ = 0;

  uint32_t framesRx_ = 0, framesTx_ = 0, checksumErrors_ = 0;
  uint32_t countersResetMs_ = 0;  // frame counters restart every kCounterWindowMs
};

}  // namespace paradox
