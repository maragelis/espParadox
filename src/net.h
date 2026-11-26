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

void netBegin();
void netLoop();
NetInfo netInfo();
