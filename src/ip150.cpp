#include "ip150.h"

#include <esp_system.h>
#include <mbedtls/aes.h>

namespace ip150 {
namespace {

constexpr uint8_t SOF = 0xAA, PAD = 0xEE;
// message types
constexpr uint8_t T_IP_RESPONSE = 0x01, T_SERIAL_RESPONSE = 0x02, T_IP_REQUEST = 0x03, T_SERIAL_REQUEST = 0x04;
// commands
constexpr uint8_t C_PASSTHROUGH = 0x00, C_CONNECT = 0xF0, C_KEEP_ALIVE = 0xF2, C_UD_CONNECT = 0xF3,
                  C_UD_DISCONNECT = 0xF4, C_TOGGLE_KEEP_ALIVE = 0xF8;
// flags (bit 0 = encrypt ... bit 7)
constexpr uint8_t F_ENCRYPT = 0x01, F_UPLOAD_DOWNLOAD = 0x02, F_INSTALLER = 0x08, F_NEWARE = 0x10,
                  F_LIVE_EVENTS = 0x20, F_KEEP_ALIVE = 0x40;

String password;
uint8_t key[32];  // current AES key (password, then session key), padded with 0xEE
bool logged = false, closing = false;
const char* stateText = "waiting for login";
uint8_t buf[1100];  // incoming bytes not yet parsed
size_t bufLen = 0;

void setKey(const uint8_t* k, size_t n) {
  memset(key, PAD, sizeof key);
  memcpy(key, k, n > 32 ? 32 : n);
}

// Paradox block order <-> AES order: 4x4 transpose (an involution).
void transpose(uint8_t* b) {
  uint8_t t[16];
  for (int row = 0; row < 4; row++)
    for (int i = 0; i < 4; i++) t[row * 4 + i] = b[row + i * 4];
  memcpy(b, t, 16);
}

void crypt(uint8_t* data, size_t len, bool encrypt) {  // len: multiple of 16
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (encrypt) mbedtls_aes_setkey_enc(&ctx, key, 256);
  else mbedtls_aes_setkey_dec(&ctx, key, 256);
  for (size_t off = 0; off + 16 <= len; off += 16) {
    uint8_t blk[16];
    memcpy(blk, data + off, 16);
    transpose(blk);
    mbedtls_aes_crypt_ecb(&ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, blk, data + off);
    transpose(data + off);
  }
  mbedtls_aes_free(&ctx);
}

void send(uint8_t type, uint8_t flags, uint8_t command, const uint8_t* payload, size_t len, const Sink& toClient) {
  uint8_t msg[16 + 64 + 16];
  if (len > 64) len = 64;  // our replies are short; panel frames are 37 bytes
  memset(msg, PAD, 16);
  msg[0] = SOF;
  msg[1] = len & 0xFF;
  msg[2] = len >> 8;
  msg[3] = type;
  msg[4] = flags;
  msg[5] = command;
  msg[6] = 0x00;  // sub-command
  msg[7] = 0x00;  // wt
  msg[8] = 0x03;  // sb
  msg[9] = 0x00;  // cryptor code (as PAI's emulator)
  size_t plen = len;
  memcpy(msg + 16, payload, len);
  if (flags & F_ENCRYPT) {
    plen = (len + 15) / 16 * 16;
    memset(msg + 16 + len, PAD, plen - len);
    crypt(msg + 16, plen, true);
  }
  toClient(msg, 16 + plen);
}

void handleLogin(const uint8_t* pl, size_t len, uint8_t inFlags, const Sink& toClient) {
  bool ok = len == password.length() && memcmp(pl, password.c_str(), len) == 0;
  uint8_t r[25];
  memset(r, 0, sizeof r);
  if (ok) {
    char session[17];
    snprintf(session, sizeof session, "%08lX%08lX", (unsigned long)esp_random(), (unsigned long)esp_random());
    r[0] = 0x00;                     // login_status: success
    memcpy(r + 1, session, 16);      // new session key (16 ASCII hex chars)
    r[17] = 0x00; r[18] = 32;        // hardware_version 32
    r[19] = 0x01;                    // IP firmware 1.32 (hex digits)
    r[20] = 0x32;
    r[21] = 0x71; r[22] = 0x23; r[23] = 0x45; r[24] = 0x67;  // IP150 serial (0x71 = IP150)
    send(T_IP_RESPONSE, (inFlags & F_ENCRYPT) | F_INSTALLER | F_NEWARE | F_LIVE_EVENTS, C_CONNECT, r, sizeof r, toClient);
    setKey((const uint8_t*)session, 16);  // reply went out with the password key; session key from now on
    logged = true;
    stateText = "logged in";
    Serial.println("[ip150] BabyWare logged in");
  } else {
    r[0] = 0x01;  // invalid_password
    r[21] = 0x71;
    send(T_IP_RESPONSE, F_INSTALLER | F_NEWARE, C_CONNECT, r, sizeof r, toClient);  // unencrypted
    closing = true;
    stateText = "wrong IP password";
    Serial.printf("[ip150] login refused: wrong IP password (got %u characters)\n", (unsigned)len);
  }
}

}  // namespace

void reset(const String& pw) {
  password = pw;
  setKey((const uint8_t*)pw.c_str(), pw.length());
  logged = closing = false;
  bufLen = 0;
  stateText = "waiting for login";
}

bool loggedIn() { return logged; }
const char* state() { return stateText; }

bool fromClient(const uint8_t* data, size_t n, const Sink& toPanel, const Sink& toClient) {
  if (bufLen + n > sizeof buf) bufLen = 0;  // garbage overflow: resync
  memcpy(buf + bufLen, data, n);
  bufLen += n;
  while (bufLen > 0) {
    if (buf[0] != SOF) {  // resync on the start byte
      size_t i = 1;
      while (i < bufLen && buf[i] != SOF) i++;
      memmove(buf, buf + i, bufLen - i);
      bufLen -= i;
      continue;
    }
    if (bufLen < 16) break;
    size_t len = buf[1] | (buf[2] << 8);
    uint8_t type = buf[3], flags = buf[4], cmd = buf[5];
    size_t wire = (flags & F_ENCRYPT) ? (len + 15) / 16 * 16 : len;
    if (wire > 1024) {  // not a valid message
      memmove(buf, buf + 1, bufLen - 1);
      bufLen--;
      continue;
    }
    if (bufLen < 16 + wire) break;  // wait for the rest
    uint8_t pl[1024];
    memcpy(pl, buf + 16, wire);
    if (flags & F_ENCRYPT) crypt(pl, wire, false);
    memmove(buf, buf + 16 + wire, bufLen - 16 - wire);
    bufLen -= 16 + wire;

    uint8_t enc = flags & F_ENCRYPT;
    if (type == T_IP_REQUEST) {
      switch (cmd) {
        case C_CONNECT:
          handleLogin(pl, len, flags, toClient);
          break;
        case C_KEEP_ALIVE: {
          uint8_t r = 0x00;
          send(T_IP_RESPONSE, enc | F_INSTALLER | F_NEWARE | F_LIVE_EVENTS, cmd, &r, 1, toClient);
          break;
        }
        case C_TOGGLE_KEEP_ALIVE: {
          uint8_t r = 0x01;
          send(T_IP_RESPONSE, enc | F_INSTALLER | F_UPLOAD_DOWNLOAD | F_NEWARE | F_LIVE_EVENTS | F_KEEP_ALIVE, cmd, &r, 1, toClient);
          break;
        }
        case C_UD_CONNECT: {
          uint8_t r[17] = {0x01};
          send(T_IP_RESPONSE, enc | F_INSTALLER | F_UPLOAD_DOWNLOAD | F_NEWARE | F_LIVE_EVENTS, cmd, r, sizeof r, toClient);
          break;
        }
        case C_UD_DISCONNECT: {
          uint8_t r = closing ? 0x01 : 0x00;
          send(T_IP_RESPONSE, enc | F_INSTALLER | F_NEWARE | F_LIVE_EVENTS, cmd, &r, 1, toClient);
          break;
        }
        default:
          Serial.printf("[ip150] unhandled IP request 0x%02X (len %u)\n", cmd, (unsigned)len);
          break;
      }
    } else if (type == T_SERIAL_REQUEST && cmd == C_PASSTHROUGH) {
      if (!logged) continue;  // nothing reaches the panel before a valid login
      toPanel(pl, len);       // BabyWare's panel frame (incl. its 0x70 close) to the serial port
    } else {
      Serial.printf("[ip150] unhandled message type 0x%02X command 0x%02X\n", type, cmd);
    }
    if (closing) return false;
  }
  return !closing;
}

void fromPanel(const uint8_t* data, size_t n, const Sink& toClient) {
  if (!logged) return;
  send(T_SERIAL_RESPONSE, F_ENCRYPT | F_UPLOAD_DOWNLOAD | F_NEWARE | F_LIVE_EVENTS | F_KEEP_ALIVE, C_PASSTHROUGH,
       data, n, toClient);
}

}  // namespace ip150
