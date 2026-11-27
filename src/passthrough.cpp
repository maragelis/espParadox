#include "passthrough.h"

#include <WiFi.h>

#include "config.h"
#include "ip150.h"
#include "display.h"
#include "mqtt.h"
#include "settings.h"

namespace {

paradox::Panel* panel = nullptr;
HardwareSerial* uart = nullptr;
WiFiServer* server = nullptr;
WiFiClient client;
bool enabled = false, session = false;
uint32_t untilMs = 0, lastDataMs = 0;
uint32_t baud = PARADOX_BAUD;
uint32_t toPanel = 0, fromPanel = 0;
String clientIp;
constexpr uint32_t kIdleMs = 10UL * 60 * 1000;  // close an idle session
// Diagnostics: the first bytes of each direction of the current / last session.
constexpr size_t kCapture = 96;
uint8_t capTo[kCapture], capFrom[kCapture];
size_t capToLen = 0, capFromLen = 0;

String hexOf(const uint8_t* b, size_t n) {
  String s;
  s.reserve(n * 3);
  char h[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(h, sizeof h, i ? " %02X" : "%02X", b[i]);
    s += h;
  }
  return s;
}

// ---- Telnet / RFC 2217 -------------------------------------------------------------------
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
constexpr uint8_t OPT_BINARY = 0, OPT_SGA = 3, OPT_COMPORT = 44;
bool telnet = false;
bool ip150Mode = false;  // BabyWare's IP150 connection (first byte 0xAA)
uint8_t panelBuf[64];   // IP150: panel bytes collected into whole messages
size_t panelLen = 0;
uint32_t panelLastMs = 0;
enum { T_DATA, T_IAC, T_OPT, T_SB, T_SB_IAC } tstate = T_DATA;
uint8_t optVerb = 0;
uint8_t sb[24];
int sbLen = 0;
uint64_t agreedDo = 0, agreedWill = 0;  // options already answered (bit per option < 64)

bool supported(uint8_t opt) { return opt == OPT_BINARY || opt == OPT_SGA || opt == OPT_COMPORT; }

void sendRaw(const uint8_t* b, size_t n) { client.write(b, n); }

void sendCmd(uint8_t verb, uint8_t opt) {
  uint8_t b[3] = {IAC, verb, opt};
  sendRaw(b, 3);
}

// RFC 2217 reply: IAC SB 44 <cmd+100> <data, 0xFF doubled> IAC SE
void sendSb(uint8_t cmd, const uint8_t* data, int len) {
  uint8_t b[40];
  int n = 0;
  b[n++] = IAC; b[n++] = SB; b[n++] = OPT_COMPORT; b[n++] = cmd + 100;
  for (int i = 0; i < len && n < 34; i++) {
    b[n++] = data[i];
    if (data[i] == IAC) b[n++] = IAC;
  }
  b[n++] = IAC; b[n++] = SE;
  sendRaw(b, n);
}

void handleOption(uint8_t verb, uint8_t opt) {
  uint64_t bit = opt < 64 ? (1ULL << opt) : 0;
  if (verb == WILL || verb == WONT) {  // client offers / refuses something
    if (verb == WILL && bit && (agreedDo & bit)) return;  // already answered
    if (verb == WILL && bit) agreedDo |= bit;
    if (verb == WILL) sendCmd(supported(opt) ? DO : DONT, opt);
  } else {  // DO / DONT: client asks us
    if (verb == DO && bit && (agreedWill & bit)) return;
    if (verb == DO && bit) agreedWill |= bit;
    if (verb == DO) sendCmd(supported(opt) ? WILL : WONT, opt);
  }
}

void handleSb() {
  if (sbLen < 2 || sb[0] != OPT_COMPORT) return;
  uint8_t cmd = sb[1];
  const uint8_t* d = sb + 2;
  int n = sbLen - 2;
  switch (cmd) {
    case 1: {  // SET-BAUDRATE (0 = query)
      if (n >= 4) {
        uint32_t v = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
        if (v >= 1200 && v <= 115200 && v != baud) {
          baud = v;
          uart->updateBaudRate(baud);
          Serial.printf("[passthrough] client set %lu baud\n", (unsigned long)baud);
        }
      }
      uint8_t r[4] = {(uint8_t)(baud >> 24), (uint8_t)(baud >> 16), (uint8_t)(baud >> 8), (uint8_t)baud};
      sendSb(1, r, 4);
      break;
    }
    case 2: { uint8_t r = 8; sendSb(2, &r, 1); break; }  // DATASIZE: 8
    case 3: { uint8_t r = 1; sendSb(3, &r, 1); break; }  // PARITY: none
    case 4: { uint8_t r = 1; sendSb(4, &r, 1); break; }  // STOPSIZE: 1
    case 5: { uint8_t r = (n >= 1 && d[0]) ? d[0] : 1; sendSb(5, &r, 1); break; }  // SET-CONTROL: echo (no flow control)
    case 8: case 9: sendSb(cmd, nullptr, 0); break;  // FLOWCONTROL-SUSPEND / RESUME
    case 10: case 11: sendSb(cmd, d, n > 0 ? 1 : 0); break;  // line / modem state masks: echo
    case 12:  // PURGE-DATA
      if (n >= 1 && (d[0] == 1 || d[0] == 3)) while (uart->available()) uart->read();
      sendSb(12, d, n > 0 ? 1 : 0);
      break;
    default: break;  // NOTIFY-* from the client and anything else: ignore
  }
}

// Feed one byte from the client; returns true (and sets `out`) for a data byte for the panel.
bool fromClient(uint8_t b, uint8_t& out) {
  switch (tstate) {
    case T_DATA:
      if (telnet && b == IAC) { tstate = T_IAC; return false; }
      out = b;
      return true;
    case T_IAC:
      if (b == IAC) { tstate = T_DATA; out = IAC; return true; }  // escaped 0xFF
      if (b == WILL || b == WONT || b == DO || b == DONT) { optVerb = b; tstate = T_OPT; return false; }
      if (b == SB) { sbLen = 0; tstate = T_SB; return false; }
      tstate = T_DATA;  // NOP, GA, ... ignored
      return false;
    case T_OPT:
      handleOption(optVerb, b);
      tstate = T_DATA;
      return false;
    case T_SB:
      if (b == IAC) tstate = T_SB_IAC;
      else if (sbLen < (int)sizeof sb) sb[sbLen++] = b;
      return false;
    case T_SB_IAC:
      if (b == SE) { handleSb(); tstate = T_DATA; }
      else if (b == IAC) { if (sbLen < (int)sizeof sb) sb[sbLen++] = IAC; tstate = T_SB; }
      else tstate = T_DATA;
      return false;
  }
  return false;
}

// ---- session -----------------------------------------------------------------------------

void endSession(const char* why) {
  if (!session) return;
  client.stop();
  session = false;
  if (baud != PARADOX_BAUD) uart->updateBaudRate(PARADOX_BAUD);
  baud = PARADOX_BAUD;
  Serial.printf("[passthrough] session ended (%s): %lu bytes to / %lu from the panel\n", why,
                (unsigned long)toPanel, (unsigned long)fromPanel);
  // Stay in BabyWare mode: BabyWare may reconnect. Only a reboot restores normal operation.
}

void startSession(WiFiClient c) {
  client = c;
  client.setNoDelay(true);
  session = true;
  telnet = false;
  ip150Mode = false;
  panelLen = 0;
  tstate = T_DATA;
  agreedDo = agreedWill = 0;
  baud = PARADOX_BAUD;
  toPanel = fromPanel = 0;
  capToLen = capFromLen = 0;
  clientIp = client.remoteIP().toString();
  lastDataMs = millis();
  while (uart->available()) uart->read();  // nothing stale for BabyWare
  Serial.printf("[passthrough] BabyWare client %s connected\n", clientIp.c_str());
}

}  // namespace

void passthroughBegin(paradox::Panel& p, HardwareSerial& u) {
  panel = &p;
  uart = &u;
}

bool passthroughMode() { return enabled; }

void passthroughEnable(uint32_t minutes) {
  if (!enabled) {
    // Enter BabyWare mode: stop everything that uses the panel or the network
    // except the web page. Only a reboot brings it back.
    panel->beginPassthrough();  // close the bridge's panel session, leave the serial port alone
    mqttShutdown();
    displayFreeze("BabyWare mode", "reboot to exit");
    // Drop the panel's answer to the close (line quiet for 300 ms, at most 1 s).
    uint32_t t0 = millis(), quiet = millis();
    while (millis() - t0 < 1000 && millis() - quiet < 300) {
      if (uart->available()) {
        uart->read();
        quiet = millis();
      }
      delay(1);
    }
  }
  if (!server) server = new WiFiServer(cfg.passthroughPort);
  if (!enabled) {
    server->begin();
    server->setNoDelay(true);
  }
  enabled = true;
  untilMs = millis() + minutes * 60000UL;
  Serial.printf("[passthrough] on for %lu min, port %u\n", (unsigned long)minutes, cfg.passthroughPort);
}

void passthroughLoop() {
  if (!enabled) return;
  if (!session && (int32_t)(millis() - untilMs) >= 0) {  // waited long enough without BabyWare
    Serial.println("[passthrough] BabyWare mode time over - rebooting to normal operation");
    delay(200);
    ESP.restart();
  }
  if (server->hasClient()) {
    WiFiClient c = server->available();
    if (session) c.stop();  // one client at a time
    else startSession(c);
  }
  if (!session) return;
  if (!client.connected()) {
    endSession("client disconnected");
    // the waiting time restarts so BabyWare can reconnect
    if ((int32_t)(millis() - untilMs) >= -60000) untilMs = millis() + 5UL * 60000;
    return;
  }

  // client -> panel
  uint8_t in[256], out[256];
  int n = client.available();
  if (n > 0) {
    n = client.read(in, n > (int)sizeof in ? sizeof in : n);
    bool first = n > 0 && toPanel == 0 && fromPanel == 0 && !ip150Mode && !telnet;
    if (first && in[0] == IAC) telnet = true;  // RFC 2217 client
    if (first && in[0] == 0xAA) {              // BabyWare IP150 connection
      ip150Mode = true;
      ip150::reset(cfg.ip150Password);
      Serial.println("[passthrough] BabyWare IP150 protocol");
    }
    if (ip150Mode) {
      for (int i = 0; i < n && capToLen < kCapture; i++) capTo[capToLen++] = in[i];
      bool keep = ip150::fromClient(
          in, n,
          [](const uint8_t* d, size_t len) {  // BabyWare's panel frame -> serial port
            uart->write(d, len);
            toPanel += len;
          },
          [](const uint8_t* d, size_t len) { client.write(d, len); });
      lastDataMs = millis();
      if (!keep) {
        client.flush();
        delay(50);
        endSession("IP150 login refused");
        return;
      }
      n = 0;  // handled
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
      uint8_t b;
      if (fromClient(in[i], b)) out[k++] = b;
    }
    if (k) {
      uart->write(out, k);
      toPanel += k;
      for (int i = 0; i < k && capToLen < kCapture; i++) capTo[capToLen++] = out[i];
    }
    lastDataMs = millis();
  }

  // panel -> client: IP150 wraps whole panel messages (37 bytes, or what came within 20 ms)
  if (ip150Mode) {
    while (uart->available() && panelLen < sizeof panelBuf) {
      panelBuf[panelLen++] = uart->read();
      panelLastMs = millis();
    }
    auto flush = [](size_t len) {
      ip150::fromPanel(panelBuf, len, [](const uint8_t* d, size_t l) { client.write(d, l); });
      fromPanel += len;
      for (size_t i = 0; i < len && capFromLen < kCapture; i++) capFrom[capFromLen++] = panelBuf[i];
      memmove(panelBuf, panelBuf + len, panelLen - len);
      panelLen -= len;
      lastDataMs = millis();
    };
    while (panelLen >= paradox::kMsgLen) flush(paradox::kMsgLen);
    if (panelLen && millis() - panelLastMs > 20) flush(panelLen);
    if (millis() - lastDataMs > kIdleMs) endSession("idle 10 min");
    return;
  }

  // panel -> client (0xFF doubled for Telnet clients)
  n = uart->available();
  if (n > 0) {
    n = uart->readBytes(in, n > 120 ? 120 : n);
    int k = 0;
    for (int i = 0; i < n; i++) {
      out[k++] = in[i];
      if (telnet && in[i] == IAC) out[k++] = IAC;
    }
    client.write(out, k);
    fromPanel += n;
    for (int i = 0; i < n && capFromLen < kCapture; i++) capFrom[capFromLen++] = in[i];
    lastDataMs = millis();
  }

  if (millis() - lastDataMs > kIdleMs) endSession("idle 10 min");
}

PassthroughInfo passthroughInfo() {
  PassthroughInfo i;
  i.enabled = enabled;
  i.connected = session;
  i.port = cfg.passthroughPort;
  i.secondsLeft = enabled && (int32_t)(untilMs - millis()) > 0 ? (untilMs - millis()) / 1000 : 0;
  i.client = session ? clientIp : String();
  i.rfc2217 = telnet;
  i.protocol = ip150Mode ? "IP150" : telnet ? "RFC 2217" : (toPanel || fromPanel) ? "raw" : "";
  i.ip150State = ip150Mode ? ip150::state() : "";
  i.baud = baud;
  i.bytesToPanel = toPanel;
  i.bytesFromPanel = fromPanel;
  i.firstToPanel = hexOf(capTo, capToLen);
  i.firstFromPanel = hexOf(capFrom, capFromLen);
  return i;
}
