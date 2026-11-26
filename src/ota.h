#pragma once
// Over-the-air firmware updates:
//  - ArduinoOTA (PlatformIO "ota" environment), password = web password
//  - Web upload on the Settings page (/api/update) - see web.cpp
// Both write the inactive app slot; the bridge reboots into it when done.

void otaBegin();
void otaLoop();
void otaSetPassword(const char* password);  // after a web password change
void otaProgress(const char* source, unsigned done, unsigned total);  // display + log
