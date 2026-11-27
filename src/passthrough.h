#pragma once
// BabyWare serial pass-through: a TCP port that lends the panel's serial port to one
// network client (a virtual COM port on the PC). Clients that speak RFC 2217 (Telnet
// COM-port control) can set the speed; raw TCP works too.
// BabyWare mode: switching it on stops everything except WiFi, the web page and the
// pass-through (panel driver, MQTT, HomeKit, network OTA, display pages). BabyWare may
// connect / reconnect freely. Only a reboot restores normal operation; if no client is
// connected when the waiting time ends, the bridge reboots by itself.
// Off by default and never saved as "on": always off after a reboot.

#include <Arduino.h>

#include "paradox.h"

struct PassthroughInfo {
  bool enabled = false;    // accepting / in a session
  bool connected = false;  // a client is connected
  uint32_t secondsLeft = 0;
  uint16_t port = 0;
  String client;           // client IP
  bool rfc2217 = false;    // client uses Telnet COM-port control
  String protocol;         // "IP150" (BabyWare IP connection), "RFC 2217" or "raw"
  String ip150State;       // IP150: waiting for login / logged in / wrong IP password
  uint32_t baud = 9600;
  uint32_t bytesToPanel = 0, bytesFromPanel = 0;
  String firstToPanel, firstFromPanel;  // first bytes of the session as hex (diagnostics)
};

void passthroughBegin(paradox::Panel& panel, HardwareSerial& uart);
void passthroughLoop();
void passthroughEnable(uint32_t minutes);  // enter BabyWare mode; reboot after this long without a client
bool passthroughMode();                    // BabyWare mode active (normal services stopped)
PassthroughInfo passthroughInfo();
