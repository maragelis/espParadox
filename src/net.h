#pragma once
#include <Arduino.h>

struct NetInfo {
  bool staConfigured;  // WIFI_SSID set
  bool staConnected;
  bool apActive;
  String ssid;  // joined network
  String ip;    // station IP
  int rssi;
  String gateway, subnet, dns1, dns2;  // current (DHCP or static) values
  String wantedSsid;  // configured network name
  String apSsid;
  String apPassword;
  String apIp;
  bool timeValid;  // NTP time available
};

// Network name = Settings hostname + "-" + the last number of the IP address
// (e.g. paradox-84 -> http://paradox-84.local), so two bridges on one network differ.
// DHCP: the last address is remembered, so the name is known before the router answers;
// if the address changes the mDNS name follows at once (DHCP / HomeKit name: next boot).
String netHostname();
String netSuffix();                         // "-84" (empty until the IP number is known)
String netWithSuffix(const String& base);   // base + suffix (not doubled), e.g. MQTT base topic

void netBegin();
void netLoop();
NetInfo netInfo();
