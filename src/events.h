#pragma once
#include <Arduino.h>

#include "paradox.h"

// Human-readable text for a 0xE0 live event (event group / sub-group table
// from the MG/SP programming guide).
void describeEvent(const paradox::Panel& panel, const paradox::Event& e, char* out, size_t n);

// User label for events that carry a user number (armed by, disarmed by, ...),
// otherwise nullptr.
const char* eventUserName(const paradox::Panel& panel, const paradox::Event& e);

// Fills `out` with the names of active troubles from status page 0.
// Returns the number written.
int listTroubles(const paradox::SystemStatus& s, const char** out, int max);
