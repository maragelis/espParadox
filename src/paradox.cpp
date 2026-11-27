#include "paradox.h"

#include "settings.h"

namespace paradox {

namespace {

constexpr uint32_t kReplyTimeoutMs = 1200;
constexpr uint32_t kPollGapMs = 80;          // pause between requests while online
constexpr uint32_t kInterByteResetMs = 40;   // a 37-byte frame takes ~39 ms at 9600 baud
constexpr uint8_t kMaxTimeouts = 4;

// Label locations in EEPROM (16 bytes each). Not part of the protocol document;
// taken from the open-source Paradox Alarm Interface (PAI) Spectra/Magellan map.
constexpr uint16_t kZoneLabelAddr = 0x0010;
constexpr uint16_t kPgmLabelAddr = 0x0210;
constexpr uint16_t kPartitionLabelAddr = 0x0310;
constexpr uint16_t kUserLabelAddr = 0x0330;  // 32 x 16 bytes (PAI map)
constexpr uint16_t kZoneDefAddr = 0x0730;  // 3 bytes per zone

const char* const kZoneDefNames[] = {
    "Disabled", "Entry delay 1", "Entry delay 2", "Entry delay 1 full", "Entry delay 2 full",
    "Follow", "Follow sleep/full", "Follow full", "Instant", "Instant sleep/full",
    "Instant full", "Instant fire", "Delayed fire", "Instant fire silent", "Delayed fire silent",
    "24h buzzer", "24h burglary", "24h hold-up", "24h gas", "24h heat", "24h water",
    "24h freeze", "24h panic", "Follow no pre-alarm", "Instant no pre-alarm",
    "Keyswitch maintained", "Keyswitch momentary"};

uint8_t checksum(const uint8_t* m) {
  uint8_t s = 0;
  for (size_t i = 0; i < kMsgLen - 1; i++) s += m[i];
  return s;
}

uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

// "1234" -> {0x12, 0x34}
void encodeBcd(const char* s, uint8_t* out, size_t nbytes) {
  size_t len = strlen(s);
  for (size_t i = 0; i < nbytes; i++) {
    uint8_t hi = (2 * i < len) ? hexNibble(s[2 * i]) : 0;
    uint8_t lo = (2 * i + 1 < len) ? hexNibble(s[2 * i + 1]) : 0;
    out[i] = (hi << 4) | lo;
  }
}

bool testBit(const uint8_t* bytes, int index) { return bytes[index / 8] & (1 << (index % 8)); }

// Copy a 16-byte EEPROM label if it looks like text; otherwise keep the default.
void applyLabel(char* dst, const uint8_t* src) {
  char tmp[kLabelLen + 1];
  size_t n = 0;
  for (size_t i = 0; i < kLabelLen; i++) {
    uint8_t c = src[i];
    if (c == 0x00 || c == 0xFF) break;
    if (c < 0x20 || c > 0x7E) return;  // not a label - leave the default
    tmp[n++] = (char)c;
  }
  while (n > 0 && tmp[n - 1] == ' ') n--;
  size_t start = 0;
  while (start < n && tmp[start] == ' ') start++;
  if (start >= n) return;
  memcpy(dst, tmp + start, n - start);
  dst[n - start] = '\0';
}

}  // namespace

// Error code in byte 2 of a 0x7X refusal (names from the PAI project's ErrorMessage).
const char* loginErrorName(uint8_t code) {
  switch (code) {
    case 0x00: return "requested command failed";
    case 0x01: return "invalid user code";
    case 0x02: return "area in code lockout";
    case 0x05: return "panel will disconnect";
    case 0x10: return "panel not connected";
    case 0x11: return "panel already connected";
    case 0x12: return "invalid PC password";
    case 0x13: return "connection mode not accepted (WinLoad on phone line)";
  }
  return "unknown error";
}

const char* linkStateName(LinkState s) {
  switch (s) {
    case LinkState::Disconnected: return "disconnected";
    case LinkState::Starting: return "starting";
    case LinkState::Initializing: return "logging in";
    case LinkState::Online: return "online";
  }
  return "?";
}

const char* zoneDefinitionName(uint8_t def) {
  if (def == kDefUnknown) return "Unknown";
  if (def < sizeof(kZoneDefNames) / sizeof(kZoneDefNames[0])) return kZoneDefNames[def];
  switch (def) {
    case 33: return "Instant no pre-alarm (stay)";
    case 34: return "Instant no pre-alarm (sleep)";
    case 35: return "Entry delay 1 stay/full instant";
    case 36: return "Entry delay 1 full instant";
  }
  return "Special";
}

const char* zoneAlarmTypeName(uint8_t type) {
  switch (type) {
    case 0: return "Audible steady";
    case 1: return "Silent";
    case 2: return "Audible pulsed";
    case 3: return "Report only";
  }
  return "?";
}

const char* PanelInfo::model() const {
  switch (productId) {
    case 0x15: return "SP5500";
    case 0x16: return "SP6000";
    case 0x17: return "SP7000";
    case 0x40: return "MG5000";
    case 0x41: return "MG5050";
  }
  return "Unknown";
}

const char* Zone::delayName() const {
  switch (delayMode()) {
    case 1: return "entry";
    case 2: return "intellizone";
    case 3: return "fire";
  }
  return "none";
}

const char* Partition::modeName() const {
  if (!armed()) return "disarmed";
  if (stayArmed()) return "stay";
  if (sleepArmed()) return "sleep";
  return "full";
}

// ---------------------------------------------------------------------------

void Panel::begin(HardwareSerial& port) {
  port_ = &port;
  for (int i = 0; i < kZones; i++) snprintf(zones_[i].label, sizeof zones_[i].label, "Zone %02d", i + 1);
  for (int i = 0; i < kPgms; i++) snprintf(pgms_[i].label, sizeof pgms_[i].label, "PGM %02d", i + 1);
  for (int i = 0; i < kPartitions; i++)
    snprintf(partitions_[i].label, sizeof partitions_[i].label, "Area %d", i + 1);
  for (int i = 0; i < kUsers; i++) snprintf(users_[i], sizeof users_[i], "User %02d", i + 1);
  strlcpy(lastError_, "Waiting for panel", sizeof lastError_);
  nextAttemptMs_ = millis() + 1000;
}

const Event& Panel::event(int i) const {
  int idx = (eventHead_ - 1 - i + kMaxEvents) % kMaxEvents;
  return events_[idx];
}

void Panel::prepare(uint8_t* m) const {
  // Source ID (protocol table 1): the bridge is wired straight to the panel, so
  // 0x01 Winload direct (PC password login) or 0x05 NEware direct (user code login).
  m[33] = cfg.loginMethod == 1 ? 0x05 : 0x01;
  // User ID: unused with a PC password login; with a user code, the user number
  // the panel answered for that code.
  uint16_t uid = cfg.loginMethod == 1 ? loginUser_ : 0;
  m[34] = (uid >> 8) & 0xFF;
  m[35] = uid & 0xFF;
  m[36] = checksum(m);
}

void Panel::dump(const char* dir, const uint8_t* m) {
  if (!rawDump) return;
  Serial.printf("[%s]", dir);
  for (size_t i = 0; i < kMsgLen; i++) Serial.printf(" %02X", m[i]);
  Serial.println();
}

void Panel::send(const uint8_t* m, Pending kind) {
  // The panel's programming is read-only for this program. Only these commands
  // may be sent: 0x5F start, 0x00 init, 0x30 set clock, 0x40 action (arm etc.),
  // 0x50 status / EEPROM *read*, 0x70 close. 0x60 (EEPROM write), 0x80 and 0x90
  // are never sent.
  switch (m[0]) {
    case 0x5F: case 0x00: case 0x30: case 0x40: case 0x50: case 0x70: break;
    default:
      Serial.printf("[paradox] BLOCKED command 0x%02X (panel configuration is read-only)\n", m[0]);
      return;
  }
  memcpy(pendingFrame_, m, kMsgLen);
  port_->write(m, kMsgLen);
  pending_ = kind;
  sentAtMs_ = millis();
  framesTx_++;
  dump("TX", m);
}

bool Panel::enqueue(const uint8_t* frame, Pending kind, const char* desc) {
  if (state_ != LinkState::Online || qCount_ >= kQueueLen) return false;
  Queued& q = queue_[(qHead_ + qCount_) % kQueueLen];
  memcpy(q.frame, frame, kMsgLen);
  q.kind = kind;
  strlcpy(q.desc, desc, sizeof q.desc);
  qCount_++;
  return true;
}

bool Panel::action(uint8_t code, uint8_t arg, const char* desc) {
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x40;
  m[2] = code;
  m[3] = arg;  // partition / zone / beep type / PGM number
  prepare(m);
  char d[32];
  snprintf(d, sizeof d, "%s %u", desc, arg + ((code == 0x20) ? 0 : 1));
  return enqueue(m, Pending::Action, d);
}

bool Panel::setTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x30;
  m[4] = year / 100;
  m[5] = year % 100;
  m[6] = month;
  m[7] = day;
  m[8] = hour;
  m[9] = minute;
  prepare(m);
  return enqueue(m, Pending::SetTime, "Set time");
}

void Panel::startSession() {
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x5F;
  m[1] = 0x20;  // extra validation byte
  prepare(m);
  rxLen_ = 0;
  state_ = LinkState::Starting;
  send(m, Pending::Start);
}

void Panel::sendInit() {
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x00;
  m[4] = info_.productId;
  m[5] = info_.fwVersion;
  m[6] = info_.fwRevision;
  m[7] = info_.fwBuild;
  m[8] = info_.panelId[0];
  m[9] = info_.panelId[1];
  encodeBcd(cfg.pcPassword.c_str(), &m[10], 2);
  if (cfg.loginMethod == 1) {
    m[13] = 0x55;  // source mode: NEware connection - log in with a user code
    String code = cfg.userCode;
    if (codeRightAligned_)
      while (code.length() < 6) code = "0" + code;
    encodeBcd(code.c_str(), &m[14], 3);  // bytes 14-16: code digits
  } else {
    m[13] = 0x00;  // source mode: Winload connection - log in with the PC password
  }
  prepare(m);
  state_ = LinkState::Initializing;
  send(m, Pending::Init);
}

void Panel::sendNext() {
  if (qCount_ > 0) {
    Queued& q = queue_[qHead_];
    strlcpy(pendingDesc_, q.desc, sizeof pendingDesc_);
    send(q.frame, q.kind);
    qHead_ = (qHead_ + 1) % kQueueLen;
    qCount_--;
    return;
  }
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x50;
  if (configBlock_ < kConfigBlocks) {
    uint16_t addr = configBlockAddr(configBlock_);
    m[2] = (addr >> 8) & 0x7F;  // < 0x80 = EEPROM *read* (never 0x60 write)
    m[3] = addr & 0xFF;
    prepare(m);
    send(m, Pending::Eeprom);
    return;
  }
  m[2] = 0x80;  // status request, not an EEPROM read
  m[3] = statusPage_;
  prepare(m);
  send(m, Pending::Status);
}

void Panel::dropLink(const char* why, uint32_t retryMs) {
  strlcpy(lastError_, why, sizeof lastError_);
  state_ = LinkState::Disconnected;
  pending_ = Pending::None;
  for (auto& w : watches_) w.known = false;  // fresh baseline after reconnecting
  qCount_ = 0;
  nextAttemptMs_ = millis() + retryMs;
  Serial.printf("[paradox] link down: %s\n", why);
}

void Panel::disconnect() {
  if (stopped_) return;
  if (state_ != LinkState::Disconnected) sendClose();  // fire and forget
  stopped_ = true;
  dropLink("Disconnected by user", 0);
}

// 0x70 close connection (also used to end a stale session the panel still holds).
void Panel::sendClose() {
  if (!port_) return;
  uint8_t m[kMsgLen] = {0};
  m[0] = 0x70;
  m[2] = 0x05;  // validation byte
  prepare(m);
  port_->write(m, kMsgLen);
  framesTx_++;
  dump("TX", m);
}

void Panel::connect() {
  if (!stopped_) return;
  stopped_ = false;
  refusals_ = 0;
  codeRightAligned_ = false;
  closedStaleSession_ = false;
  strlcpy(lastError_, "Connecting", sizeof lastError_);
  nextAttemptMs_ = millis();
  Serial.println("[paradox] connect requested");
}

void Panel::onTimeout() {
  Pending was = pending_;
  pending_ = Pending::None;
  if (was == Pending::Start || was == Pending::Init) {
    dropLink(was == Pending::Start ? "No response from panel" : "No answer to login", 5000);
    return;
  }
  if (was == Pending::Action || was == Pending::SetTime)
    snprintf(lastAction_, sizeof lastAction_, "%s: no answer", pendingDesc_);
  if (was == Pending::Eeprom) {
    // Answers occasionally get lost: retry the same block twice, then skip it.
    if (++configRetries_ > 2) {
      Serial.printf("[paradox] no answer reading 0x%04X - skipped\n", configBlockAddr(configBlock_));
      configRetries_ = 0;
      configBlockFailed_ = true;
      if (++configBlock_ >= kConfigBlocks) finishConfigRead();
    }
  }
  if (++timeouts_ >= kMaxTimeouts) dropLink("Panel stopped responding", 2000);
  lastReplyMs_ = millis();
}

void Panel::beginPassthrough() {
  if (passthrough_) return;
  if (state_ != LinkState::Disconnected) sendClose();  // end our session cleanly
  passthrough_ = true;
  stopped_ = true;
  dropLink("BabyWare session (pass-through)", 0);
}

void Panel::endPassthrough() {
  if (!passthrough_) return;
  passthrough_ = false;
  rxLen_ = 0;
  connect();  // log in again
}

void Panel::loop() {
  if (!port_ || passthrough_) return;  // pass-through owns the serial port
  readSerial();
  uint32_t now = millis();
  if (now - countersResetMs_ >= kCounterWindowMs) {  // keep the counters small and recent
    framesRx_ = framesTx_ = checksumErrors_ = 0;
    countersResetMs_ = now;
  }
  if (state_ == LinkState::Disconnected) {
    if (!stopped_ && (int32_t)(now - nextAttemptMs_) >= 0) startSession();
    return;
  }
  if (pending_ != Pending::None) {
    if (now - sentAtMs_ > kReplyTimeoutMs) onTimeout();
  } else if (state_ == LinkState::Online && now - lastReplyMs_ >= kPollGapMs) {
    sendNext();
  }
}

void Panel::readSerial() {
  while (port_->available()) {
    uint32_t now = millis();
    if (rxLen_ > 0 && now - rxByteMs_ > kInterByteResetMs) rxLen_ = 0;  // stale partial frame
    rxByteMs_ = now;
    rx_[rxLen_++] = (uint8_t)port_->read();
    if (rxLen_ < kMsgLen) continue;
    if (checksum(rx_) == rx_[kMsgLen - 1]) {
      uint8_t frame[kMsgLen];
      memcpy(frame, rx_, kMsgLen);
      rxLen_ = 0;
      framesRx_++;
      lastRxMs_ = now;
      dump("RX", frame);
      handleFrame(frame);
    } else {
      // Out of sync: drop one byte and keep looking for a valid frame.
      checksumErrors_++;
      memmove(rx_, rx_ + 1, kMsgLen - 1);
      rxLen_ = kMsgLen - 1;
    }
  }
}

void Panel::handleFrame(const uint8_t* m) {
  if (m[0] == 0xE0) {  // live event, may arrive at any time
    handleEvent(m);
    return;
  }
  uint8_t hi = m[0] >> 4;
  systemAlarm_ = m[0] & 0x04;
  if (pending_ == Pending::None) return;  // unsolicited reply, ignore

  uint8_t expectHi = pendingFrame_[0] >> 4;
  if (pending_ == Pending::Start) expectHi = 0x0;
  if (pending_ == Pending::Init) expectHi = 0x1;

  if (hi == 0x7 && expectHi != 0x7) {
    // Error / close-connection answer: the panel refused the last command.
    Pending was = pending_;
    pending_ = Pending::None;
    lastReplyMs_ = millis();
    if (was == Pending::Start || was == Pending::Init) {
      refusals_++;
      lastLoginError_ = m[2];
      char why[96];
      Serial.printf("[paradox] login refused: error 0x%02X (%s)%s\n", m[2], loginErrorName(m[2]),
                    cfg.loginMethod == 1 ? "" : "");
      if (m[2] == 0x13 && !closedStaleSession_) {
        // The panel still has an earlier session open (e.g. before a reboot / login change):
        // close it (0x70) and log in again.
        closedStaleSession_ = true;
        refusals_--;
        sendClose();
        dropLink("Panel had an old session open - closed it, logging in again", 3000);
      } else if (cfg.loginMethod == 1 && m[2] == 0x01 && !codeRightAligned_ && cfg.userCode.length() < 6) {
        // "Invalid user code": the document doesn't say how a short code is packed - try the other layout once.
        codeRightAligned_ = true;
        dropLink("Panel refused the user code - trying the other code layout", 3000);
      } else if (cfg.loginMethod == 1 || refusals_ >= 3) {
        stopped_ = true;  // don't keep retrying a wrong code / password
        snprintf(why, sizeof why, "Panel refused login: %s (0x%02X) - press Connect panel to retry",
                 loginErrorName(m[2]), m[2]);
        dropLink(why, 0);
      } else {
        snprintf(why, sizeof why, "Panel refused login: %s (0x%02X)", loginErrorName(m[2]), m[2]);
        dropLink(why, 15000);
      }
    } else if (was == Pending::Action || was == Pending::SetTime) {
      snprintf(lastAction_, sizeof lastAction_, "%s: refused", pendingDesc_);
    } else if (was == Pending::Eeprom) {
      // EEPROM read not allowed - keep cached / default configuration.
      configBlockFailed_ = true;
      configBlock_ = kConfigBlocks;
      Serial.println("[paradox] panel refused EEPROM read - using cached configuration");
    }
    return;
  }
  if (hi != expectHi) return;  // not the answer we're waiting for

  Pending was = pending_;
  if (was == Pending::Status && (m[2] != 0x80 || m[3] != pendingFrame_[3])) return;
  if (was == Pending::Eeprom && (m[2] != pendingFrame_[2] || m[3] != pendingFrame_[3])) return;

  pending_ = Pending::None;
  timeouts_ = 0;
  lastReplyMs_ = millis();

  switch (was) {
    case Pending::Start:
      info_.valid = true;
      info_.productId = m[4];
      info_.fwVersion = m[5];
      info_.fwRevision = m[6];
      info_.fwBuild = m[7];
      info_.panelId[0] = m[8];
      info_.panelId[1] = m[9];
      Serial.printf("[paradox] panel %s (0x%02X) fw %u.%u.%u\n", info_.model(), info_.productId,
                    info_.fwVersion, info_.fwRevision, info_.fwBuild);
      sendInit();
      break;
    case Pending::Init:
      // NEware answer: byte 3 = user number for the code, byte 4 = area rights (bit 0 = area 1).
      loginUser_ = cfg.loginMethod == 1 ? m[3] : 0;
      loginAreas_ = cfg.loginMethod == 1 ? (m[4] & 0x03) : 0;
      if (cfg.loginMethod == 1)
        Serial.printf("[paradox] logged in with user code: user %u, areas 0x%02X\n", loginUser_, loginAreas_);
      if (cfg.loginMethod == 1)
        Serial.printf("[paradox] user-code login accepted (%s code layout)\n", codeRightAligned_ ? "right-aligned" : "left-aligned");
      closedStaleSession_ = false;
      lastLoginError_ = 0xFF;
      refusals_ = 0;
      state_ = LinkState::Online;
      lastError_[0] = '\0';
      statusPage_ = 0;
      configBlockFailed_ = false;
      configBlock_ = cfg.readLabels ? 0 : kConfigBlocks;
      Serial.println("[paradox] logged in");
      break;
    case Pending::Status:
      handleStatus(m);
      break;
    case Pending::Eeprom:
      handleEeprom(m);
      break;
    case Pending::Action:
    case Pending::SetTime:
      snprintf(lastAction_, sizeof lastAction_, "%s: OK", pendingDesc_);
      Serial.printf("[paradox] %s\n", lastAction_);
      break;
    case Pending::None:
      break;
  }
}

void Panel::handleStatus(const uint8_t* m) {
  uint8_t page = m[3];
  sys_.pagesSeen |= (1 << page);
  statusPage_ = (page + 1) % 6;

  switch (page) {
    case 0:
      memcpy(sys_.trouble, &m[4], 5);
      sys_.year = m[9] * 100 + m[10];
      sys_.month = m[11];
      sys_.day = m[12];
      sys_.hour = m[13];
      sys_.minute = m[14];
      sys_.acLevel = m[15];
      sys_.dcLevel = m[16];
      sys_.batteryLevel = m[17];
      sys_.rfNoise = m[18];
      for (int z = 0; z < kZones; z++) {
        zones_[z].open = testBit(&m[19], z);
        zones_[z].tamper = testBit(&m[23], z);
        zones_[z].fireTrouble = testBit(&m[31], z);
      }
      for (int p = 0; p < kPgms; p++) pgms_[p].tamper = testBit(&m[27], p);
      sys_.moduleTamper = m[29] | (m[30] << 8);
      troubleTransitions();  // after the panel time is parsed
      break;
    case 1:
      for (int z = 0; z < kZones; z++) {
        zones_[z].rfSupervision = testBit(&m[4], z);
        zones_[z].lowBattery = testBit(&m[13], z);
      }
      for (int p = 0; p < kPgms; p++) pgms_[p].rfSupervision = testBit(&m[8], p);
      sys_.moduleSupervision = m[10] | (m[11] << 8);
      sys_.repeaterKeypadSupervision = m[12];
      memcpy(partitions_[0].raw, &m[17], 4);
      memcpy(partitions_[1].raw, &m[21], 4);
      break;
    case 2:
      for (int z = 0; z < kZones; z++) zones_[z].status = m[4 + z];
      break;
    case 3:
      for (int z = 0; z < kZones; z++) zones_[z].signal = m[4 + z];
      break;
    case 4:
      for (int p = 0; p < kPgms; p++) pgms_[p].signal = m[4 + p];
      sys_.repeaterSignal[0] = m[20];
      sys_.repeaterSignal[1] = m[21];
      memcpy(sys_.keypadSignal, &m[22], 8);
      break;
    case 5:
      for (int z = 0; z < kZones; z++) zones_[z].exitDelay = testBit(&m[4], z);
      break;
  }
}

void Panel::handleEeprom(const uint8_t* m) {
  const uint8_t* data = &m[4];  // 32 bytes = two labels
  int b = configBlock_;
  if (b < kZoneLabelBlocks) {
    applyLabel(zones_[b * 2].label, data);
    applyLabel(zones_[b * 2 + 1].label, data + 16);
  } else if (b < kZoneLabelBlocks + kPgmLabelBlocks) {
    int p = (b - kZoneLabelBlocks) * 2;
    applyLabel(pgms_[p].label, data);
    applyLabel(pgms_[p + 1].label, data + 16);
  } else if (b < kFirstUserBlock) {
    applyLabel(partitions_[0].label, data);
    applyLabel(partitions_[1].label, data + 16);
  } else if (b < kFirstDefBlock) {
    int u = (b - kFirstUserBlock) * 2;
    applyLabel(users_[u], data);
    applyLabel(users_[u + 1], data + 16);
  } else {
    memcpy(&defBuf_[(b - kFirstDefBlock) * 32], data, 32);
  }
  configRetries_ = 0;
  if (++configBlock_ >= kConfigBlocks) finishConfigRead();
}

uint16_t Panel::configBlockAddr(int block) {
  if (block < kZoneLabelBlocks) return kZoneLabelAddr + block * 0x20;
  block -= kZoneLabelBlocks;
  if (block < kPgmLabelBlocks) return kPgmLabelAddr + block * 0x20;
  block -= kPgmLabelBlocks;
  if (block < kPartLabelBlocks) return kPartitionLabelAddr;
  block -= kPartLabelBlocks;
  if (block < kUserLabelBlocks) return kUserLabelAddr + block * 0x20;
  block -= kUserLabelBlocks;
  return kZoneDefAddr + block * 0x20;
}

void Panel::finishConfigRead() {
  if (configBlockFailed_) {
    // Partial read: don't trust it. Keep whatever was cached before.
    Serial.println("[paradox] configuration read incomplete - keeping cached configuration");
    return;
  }
  labelsValid_ = true;
  if (info_.knownModel()) {
    for (int z = 0; z < kZones; z++) {
      zones_[z].definition = defBuf_[z * 3];
      zones_[z].partitionRaw = defBuf_[z * 3 + 1];
      zones_[z].options = defBuf_[z * 3 + 2];
    }
    definitionsValid_ = true;
  } else {
    // Unknown model: the zone-definition address doesn't apply. Keep the labels,
    // treat every definition as unknown (zones are enabled/disabled on the bridge).
    for (int z = 0; z < kZones; z++) {
      zones_[z].definition = kDefUnknown;
      zones_[z].partitionRaw = 0;
      zones_[z].options = 0;
    }
    definitionsValid_ = false;
    Serial.printf("[paradox] product ID 0x%02X not in the protocol doc - zone definitions not used\n",
                  info_.productId);
  }
  int enabled = 0;
  for (int z = 0; z < kZones; z++) enabled += zones_[z].panelEnabled();
  Serial.printf("[paradox] configuration read: %d of %d zones enabled in the panel\n", enabled, kZones);
  if (onConfigRead) onConfigRead(snapshot());
}

bool Panel::rereadConfig() {
  if (state_ != LinkState::Online || configReading()) return false;
  configBlockFailed_ = false;
  configBlock_ = 0;
  return true;
}

PanelConfig Panel::snapshot() const {
  PanelConfig c;
  c.productId = info_.productId;
  c.labelsValid = labelsValid_;
  c.definitionsValid = definitionsValid_;
  for (int z = 0; z < kZones; z++) {
    c.zoneDef[z][0] = zones_[z].definition;
    c.zoneDef[z][1] = zones_[z].partitionRaw;
    c.zoneDef[z][2] = zones_[z].options;
    memcpy(c.zoneLabel[z], zones_[z].label, sizeof c.zoneLabel[z]);
  }
  for (int p = 0; p < kPgms; p++) memcpy(c.pgmLabel[p], pgms_[p].label, sizeof c.pgmLabel[p]);
  for (int p = 0; p < kPartitions; p++)
    memcpy(c.partitionLabel[p], partitions_[p].label, sizeof c.partitionLabel[p]);
  for (int u = 0; u < kUsers; u++) memcpy(c.userLabel[u], users_[u], sizeof c.userLabel[u]);
  return c;
}

void Panel::restore(const PanelConfig& c) {
  if (c.magic != PanelConfig::kMagic) return;
  if (c.definitionsValid && PanelInfo::productIdKnown(c.productId)) {
    for (int z = 0; z < kZones; z++) {
      zones_[z].definition = c.zoneDef[z][0];
      zones_[z].partitionRaw = c.zoneDef[z][1];
      zones_[z].options = c.zoneDef[z][2];
    }
    definitionsValid_ = true;
  }
  if (c.labelsValid) {
    for (int z = 0; z < kZones; z++) strlcpy(zones_[z].label, c.zoneLabel[z], sizeof zones_[z].label);
    for (int p = 0; p < kPgms; p++) strlcpy(pgms_[p].label, c.pgmLabel[p], sizeof pgms_[p].label);
    for (int p = 0; p < kPartitions; p++)
      strlcpy(partitions_[p].label, c.partitionLabel[p], sizeof partitions_[p].label);
    for (int u = 0; u < kUsers; u++)
      if (c.userLabel[u][0]) strlcpy(users_[u], c.userLabel[u], sizeof users_[u]);
    labelsValid_ = true;
  }
}

namespace {
constexpr uint32_t kTroubleDedupMs = 60000;
constexpr uint8_t kGroupNewTrouble = 44, kGroupTroubleRestored = 45;
}  // namespace

void Panel::handleEvent(const uint8_t* m) {
  Event e;
  e.uptimeMs = millis();
  e.year = m[1] * 100 + m[2];
  e.month = m[3];
  e.day = m[4];
  e.hour = m[5];
  e.minute = m[6];
  e.group = m[7];
  e.sub = m[8];
  e.partition = m[9];
  memcpy(e.serial, &m[10], 4);
  if (e.group == kGroupNewTrouble || e.group == kGroupTroubleRestored) {
    for (auto& w : watches_) {
      if (e.sub != w.sub) continue;
      // The bridge already logged this change from the status bit: don't log it twice.
      if (e.group == w.ownGroup && millis() - w.ownMs < kTroubleDedupMs) return;
      w.realGroup = e.group;
      w.realMs = millis();
    }
  }
  addEvent(e);
}

void Panel::addEvent(const Event& e) {
  events_[eventHead_] = e;
  eventHead_ = (eventHead_ + 1) % kMaxEvents;
  if (eventCount_ < kMaxEvents) eventCount_++;
  if (onEvent) onEvent(e);
}

// A watched trouble bit (status page 0) changed: log "New trouble: <x>" /
// "Trouble restored: <x>" with the panel's own event codes.
void Panel::troubleTransitions() {
  for (int i = 0; i < 2; i++) {
    TroubleWatch& w = watches_[i];
    bool active = watchActive(i);
    if (!w.known) {  // first status after connecting: just remember it
      w.known = true;
      w.active = active;
      continue;
    }
    if (active == w.active) continue;
    w.active = active;
    uint8_t group = active ? kGroupNewTrouble : kGroupTroubleRestored;
    Serial.printf("[paradox] %s %s\n", w.sub == 1 ? "AC power" : "Battery", active ? "trouble" : "restored");
    if (group == w.realGroup && millis() - w.realMs < kTroubleDedupMs) continue;  // panel already told us
    Event e = {};
    e.uptimeMs = millis();
    e.year = sys_.year;
    e.month = sys_.month;
    e.day = sys_.day;
    e.hour = sys_.hour;
    e.minute = sys_.minute;
    e.group = group;
    e.sub = w.sub;
    e.partition = 0;
    w.ownGroup = group;
    w.ownMs = millis();
    addEvent(e);
  }
}

}  // namespace paradox
