#pragma once
// Paradox IP150 module emulation (server side) for BabyWare's "IP150 / Static IP"
// connection, based on the open-source Paradox Alarm Interface (PAI) IP interface.
//
// Every message: 16-byte header (0xAA, length LE16 of the plain payload, message type,
// flags, command, sub-command, wt, sb, cryptor, padding 0xEE) + payload. Encrypted payloads
// use AES-256-ECB, key = password / session key padded to 32 bytes with 0xEE, blocks padded
// with 0xEE, each 16-byte block transposed (Paradox row order) before and after AES.
//
// Login (0xF0) carries the IP password encrypted with itself; the reply holds a new random
// session key (encrypted with the password) used from then on. Panel traffic travels as
// "serial passthrough" messages (type 0x04 request / 0x02 response, command 0x00).

#include <Arduino.h>

#include <functional>

namespace ip150 {

using Sink = std::function<void(const uint8_t*, size_t)>;

// New session; `password` = the IP password BabyWare must use.
void reset(const String& password);

// Bytes from BabyWare. Panel frames go to `toPanel`, protocol replies to `toClient`.
// Returns false when the connection should be closed (e.g. wrong password).
bool fromClient(const uint8_t* data, size_t n, const Sink& toPanel, const Sink& toClient);

// One complete panel message, wrapped and sent to BabyWare (only after a successful login).
void fromPanel(const uint8_t* data, size_t n, const Sink& toClient);

bool loggedIn();
const char* state();  // human readable: "waiting for login", "logged in", "wrong IP password", ...

}  // namespace ip150
