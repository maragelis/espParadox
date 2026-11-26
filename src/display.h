#pragma once
#include "paradox.h"

// TTGO T-Display (240x135) status screen.
//   Page 1: WiFi + panel connection info and partition state
//   Page 2: zone grid
//   Page 3: recent events
// BUTTON_PAGE cycles pages, BUTTON_BACKLIGHT toggles the backlight.
void displayBegin();
void displayLoop(const paradox::Panel& panel);
void displayMessage(const char* text);  // full-screen notice, shown for ~2 s
void displayProgress(const char* title, int percent);  // progress bar (OTA)
