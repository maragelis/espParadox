#include "display.h"

#include <TFT_eSPI.h>

#include "config.h"
#include "events.h"
#include "homekit.h"
#include "net.h"
#include "qrgen.h"
#include "settings.h"
#include "version.h"
#include "zones.h"

namespace {

constexpr int W = 240, H = 135;
constexpr int kBasePages = 3;  // + a HomeKit page while HomeKit is enabled
int pageCount() { return kBasePages + (cfg.homekitEnabled ? 1 : 0); }
constexpr uint32_t kRefreshMs = 250;

TFT_eSPI tft;
TFT_eSprite spr(&tft);
bool haveSprite = false;
int page = 0;
bool backlight = true;
uint32_t lastDraw = 0;
uint32_t holdUntil = 0;
bool active = false;  // display enabled in settings and initialised
bool frozen = false;  // BabyWare mode: static notice only

struct Button {
  explicit Button(uint8_t p) : pin(p) {}
  uint8_t pin;
  bool last = true;  // pulled up, true = released
  uint32_t changedAt = 0;
  bool pressed() {
    bool v = digitalRead(pin);
    uint32_t now = millis();
    if (v != last && now - changedAt > 40) {
      last = v;
      changedAt = now;
      return !v;  // falling edge
    }
    return false;
  }
};
Button btnPage{BUTTON_PAGE};
Button btnLight{BUTTON_BACKLIGHT};

uint16_t stateColor(const paradox::Panel& p) {
  if (p.systemAlarm()) return TFT_RED;
  switch (p.state()) {
    case paradox::LinkState::Online: return TFT_DARKGREEN;
    case paradox::LinkState::Disconnected: return TFT_MAROON;
    default: return TFT_ORANGE;
  }
}

void header(const paradox::Panel& p, const char* title) {
  bool alarm = p.systemAlarm();
  bool blinkOn = (millis() / 500) % 2;
  uint16_t bg = (alarm && !blinkOn) ? TFT_BLACK : stateColor(p);
  spr.fillRect(0, 0, W, 20, bg);
  spr.setTextFont(2);
  spr.setTextColor(TFT_WHITE, bg);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(title, 4, 10);
  spr.setTextDatum(MR_DATUM);
  const char* st = alarm ? "ALARM" : paradox::linkStateName(p.state());
  char buf[24];
  snprintf(buf, sizeof buf, "%s  %d/%d", st, page + 1, pageCount());
  spr.drawString(buf, W - 4, 10);
  spr.setTextDatum(TL_DATUM);
}

void row(int y, const char* key, const String& value, uint16_t color = TFT_WHITE) {
  spr.setTextFont(2);
  spr.setTextColor(TFT_SILVER, TFT_BLACK);
  spr.drawString(key, 4, y);
  spr.setTextColor(color, TFT_BLACK);
  spr.drawString(value, 52, y);
}

void drawConnection(const paradox::Panel& p) {
  header(p, "PARADOX v" FW_VERSION);
  NetInfo n = netInfo();
  int y = 23;

  // --- WiFi
  if (n.staConnected) {
    row(y, "WiFi", n.ssid + "  " + String(n.rssi) + "dBm", TFT_GREEN);
    y += 16;
    row(y, "IP", n.ip, TFT_CYAN);
    y += 16;
    row(y, "Name", netHostname() + ".local", TFT_CYAN);
    y += 16;
  } else if (n.staConfigured) {
    row(y, "WiFi", "joining " + n.wantedSsid, TFT_ORANGE);
    y += 16;
  }
  if (n.apActive) {
    row(y, "AP", n.apSsid + " / " + n.apPassword, TFT_YELLOW);
    y += 16;
    row(y, "IP", n.apIp, TFT_CYAN);
    y += 16;
  }

  // --- Panel
  const auto& info = p.info();
  char buf[64];
  if (info.valid) {
    if (info.knownModel())
      snprintf(buf, sizeof buf, "%s fw %u.%u.%u", info.model(), info.fwVersion, info.fwRevision,
               info.fwBuild);
    else
      snprintf(buf, sizeof buf, "ID 0x%02X fw %u.%u.%u", info.productId, info.fwVersion,
               info.fwRevision, info.fwBuild);
    row(y, "Panel", buf);
  } else {
    row(y, "Panel", "not detected", TFT_ORANGE);
  }
  y += 16;

  if (p.state() == paradox::LinkState::Online && p.configReading()) {
    row(y, "Link", "reading panel config...", TFT_CYAN);
  } else if (p.state() == paradox::LinkState::Online) {
    int zn = 0;
    for (int i = 0; i < paradox::kZones; i++) zn += zoneEnabled(i);
    snprintf(buf, sizeof buf, "online  %d zones  err %lu", zn, (unsigned long)p.checksumErrors());
    row(y, "Link", buf, TFT_GREEN);
  } else {
    row(y, "Link", p.lastError()[0] ? p.lastError() : paradox::linkStateName(p.state()),
        TFT_ORANGE);
  }
  y += 16;

  // --- Partitions (only once status data has arrived)
  if (y <= H - 16 && p.state() == paradox::LinkState::Online && (p.system().pagesSeen & 0x02)) {
    int x = 4;
    for (int i = 0; i < paradox::kPartitions; i++) {
      if (!partitionInUse(i)) continue;
      const auto& part = p.partition(i);
      uint16_t c = part.inAlarm() ? TFT_RED : part.armed() ? TFT_ORANGE : TFT_GREEN;
      String mode = part.inAlarm() ? "ALARM" : String(part.modeName());
      mode.toUpperCase();
      snprintf(buf, sizeof buf, "A%d %s", i + 1, mode.c_str());
      spr.setTextFont(2);
      spr.setTextColor(c, TFT_BLACK);
      spr.drawString(buf, x, y);
      x += 118;
    }
  }
}

void drawZones(const paradox::Panel& p) {
  int list[paradox::kZones], count = 0, open = 0, byp = 0;
  for (int i = 0; i < paradox::kZones; i++) {
    if (!zoneEnabled(i)) continue;  // disabled in the panel or locally
    const auto& z = p.zone(i);
    list[count++] = i;
    open += z.open;
    byp += z.bypassed();
  }
  char title[40];
  snprintf(title, sizeof title, "ZONES %d  open %d byp %d", count, open, byp);
  header(p, title);
  if (count == 0) {
    spr.setTextFont(2);
    spr.setTextColor(TFT_SILVER, TFT_BLACK);
    spr.drawString("No zones enabled", 4, 40);
    return;
  }

  const int cols = 8, top = 22;
  const int rows = (count + cols - 1) / cols;
  const int cw = W / cols, ch = min((H - top) / rows, 40);
  spr.setTextFont(2);
  spr.setTextDatum(MC_DATUM);
  for (int k = 0; k < count; k++) {
    int i = list[k];
    const auto& z = p.zone(i);
    int x = (k % cols) * cw, y = top + (k / cols) * ch;
    uint16_t bg = TFT_DARKGREY, fg = TFT_WHITE;
    if (z.alarm()) bg = TFT_RED;
    else if (z.tamper) bg = TFT_MAGENTA;
    else if (z.bypassed()) bg = TFT_BLUE;
    else if (z.open) { bg = TFT_YELLOW; fg = TFT_BLACK; }
    else if (z.wasAlarm()) bg = TFT_MAROON;
    else if (p.state() == paradox::LinkState::Online) bg = 0x0320;  // dim green = closed
    spr.fillRoundRect(x + 1, y + 1, cw - 2, ch - 2, 3, bg);
    spr.setTextColor(fg, bg);
    spr.drawNumber(i + 1, x + cw / 2, y + ch / 2);
  }
  spr.setTextDatum(TL_DATUM);
}

void drawEvents(const paradox::Panel& p) {
  header(p, "EVENTS");
  spr.setTextFont(1);
  int y = 24;
  if (p.eventCount() == 0) {
    spr.setTextColor(TFT_SILVER, TFT_BLACK);
    spr.drawString("No events received yet", 4, y);
    return;
  }
  char text[80], line[96];
  for (int i = 0; i < p.eventCount() && y < H - 8; i++) {
    const auto& e = p.event(i);
    describeEvent(p, e, text, sizeof text);
    snprintf(line, sizeof line, "%02u:%02u %s", e.hour, e.minute, text);
    line[39] = '\0';  // 40 columns of the 6px font
    uint16_t c = (e.group == 36 || e.group == 37 || e.group == 40) ? TFT_RED
                 : (e.group == 1)                                    ? TFT_YELLOW
                                                                     : TFT_WHITE;
    spr.setTextColor(c, TFT_BLACK);
    spr.drawString(line, 4, y);
    y += 11;
  }
}

// HomeKit: setup QR code (scan with the iPhone camera / Home app) + the 8-digit code.
QRCode qr;
uint8_t qrBuf[106];  // qrcode_getBufferSize(3)
String qrUri;        // payload the cached QR was made from
bool qrOk = false;

void drawHomekit(const paradox::Panel& p) {
  header(p, "HOMEKIT");
  HomekitInfo hk = homekitInfo();
  if (hk.setupUri != qrUri) {
    qrUri = hk.setupUri;
    qrOk = !qrUri.isEmpty() && qrcode_initText(&qr, qrBuf, 3, ECC_MEDIUM, qrUri.c_str()) == 0;
  }
  int tx = 4;
  if (qrOk) {
    const int scale = 3, quiet = 2, side = (qr.size + 2 * quiet) * scale;  // 99 px
    const int x0 = 4, y0 = 22 + (H - 22 - side) / 2;
    spr.fillRect(x0, y0, side, side, TFT_WHITE);
    for (int y = 0; y < qr.size; y++)
      for (int x = 0; x < qr.size; x++)
        if (qrcode_getModule(&qr, x, y))
          spr.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale, scale, scale, TFT_BLACK);
    tx = x0 + side + 6;
  }
  spr.setTextColor(TFT_SILVER, TFT_BLACK);
  spr.drawString("Setup code", tx, 26, 2);
  String code = hk.code.isEmpty() ? String("not set") : hk.code;
  int font = spr.textWidth(code, 4) <= W - tx - 2 ? 4 : 2;
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(code, tx, 44, font);
  uint16_t c = hk.paired ? TFT_GREEN : hk.started ? TFT_CYAN : TFT_ORANGE;
  spr.setTextColor(c, TFT_BLACK);
  spr.drawString(hk.status, tx, 76, 2);
  spr.setTextColor(TFT_SILVER, TFT_BLACK);
  if (hk.paired) spr.drawString(String(hk.controllers) + " controller(s)", tx, 94, 2);
  else {
    spr.drawString("Home app: Add", tx, 94, 2);
    spr.drawString("Accessory", tx, 110, 2);
  }
}

}  // namespace

void displayBegin() {
  // Other ESP32 boards: never initialise the screen or touch its pins.
  if (!cfg.displayEnabled) return;
  active = true;
  pinMode(BUTTON_PAGE, INPUT);  // GPIO35 has an external pull-up on the T-Display
  pinMode(BUTTON_BACKLIGHT, INPUT_PULLUP);
  tft.init();
  tft.setRotation(1);  // landscape, USB on the right
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  // 8-bit (256 colours) frame buffer: 32 KB instead of 65 KB at 16-bit. TFT_eSPI converts
  // the 16-bit colour constants when drawing and back when pushing to the screen.
  spr.setColorDepth(8);
  haveSprite = spr.createSprite(W, H) != nullptr;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Paradox bridge v" FW_VERSION " starting...", 4, 4, 2);
}

void displayMessage(const char* text) {
  if (!active) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, W / 2, H / 2, 2);
  tft.setTextDatum(TL_DATUM);
  holdUntil = millis() + 2000;  // keep it on screen ~2 s before the normal pages
}

void displayProgress(const char* title, int pct) {
  if (!active) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, W / 2, 40, 4);
  tft.drawRect(20, 70, W - 40, 20, TFT_WHITE);
  tft.fillRect(22, 72, (W - 44) * constrain(pct, 0, 100) / 100, 16, TFT_GREEN);
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", pct);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, W / 2, 110, 2);
  tft.setTextDatum(TL_DATUM);
  holdUntil = millis() + 5000;
}

void displayFreeze(const char* line1, const char* line2) {
  if (!active) return;
  frozen = true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(line1, W / 2, H / 2 - 16, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line2, W / 2, H / 2 + 20, 2);
  tft.setTextDatum(TL_DATUM);
}

void displayLoop(const paradox::Panel& panel) {
  if (!active) return;
  if (btnPage.pressed()) {
    page = (page + 1) % pageCount();
    lastDraw = 0;
  }
  if (btnLight.pressed()) {
    backlight = !backlight;
    digitalWrite(TFT_BL, backlight ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  }
  if (frozen) return;  // BabyWare mode notice stays
  if ((int32_t)(millis() - holdUntil) < 0) return;
  if (!haveSprite || millis() - lastDraw < kRefreshMs) return;
  lastDraw = millis();

  if (page >= pageCount()) page = 0;
  spr.fillSprite(TFT_BLACK);
  switch (page) {
    case 0: drawConnection(panel); break;
    case 1: drawZones(panel); break;
    case 2: drawEvents(panel); break;
    case 3: drawHomekit(panel); break;
  }
  spr.pushSprite(0, 0);
}
