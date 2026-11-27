#pragma once
#include "paradox.h"

// TTGO T-Display (240x135) status screen.
//   Page 1: WiFi + panel connection info and partition state
//   Page 2: zone grid
//   Page 3: recent events
//   Page 4: HomeKit setup QR code + code (only while HomeKit is enabled)
// BUTTON_PAGE cycles pages, BUTTON_BACKLIGHT toggles the backlight.
void displayBegin();
void displayLoop(const paradox::Panel& panel);
void displayMessage(const char* text);  // full-screen notice, shown for ~2 s
// Permanent full-screen notice (BabyWare mode): the normal pages stop until reboot.
void displayFreeze(const char* line1, const char* line2);
void displayProgress(const char* title, int percent);  // progress bar (OTA)
