#pragma once
// MQTT bridge. All topics start with the configured base (default "paradox").
//
// Published (retained unless noted):
//   <base>/status                    online | offline (last will)
//   <base>/info                      JSON: version, IP, panel model / firmware
//   <base>/panel/link                online | disconnected | starting | logging in
//   <base>/panel/alarm               true | false (system in alarm)
//   <base>/troubles                  JSON array of active troubles
//   <base>/panel/ac_power            true (AC present) | false (AC failure)
//   <base>/panel/battery_low         true (battery low / missing) | false
//   <base>/area/<n>/state            disarmed | stay | sleep | full | alarm
//   <base>/area/<n>/homekit          D | SA | NA | AA | T   (current state, mqttthing defaults)
//   <base>/area/<n>/homekit/target   D | SA | NA | AA       (target state)
//   <base>/area/<n>/ready            true | false
//   <base>/area/<n>/json             JSON with every area flag
//   <base>/area/<n>/ha               Home Assistant state (disarmed, armed_home, armed_away,
//                                    armed_night, arming, pending, triggered)
// Home Assistant discovery (if enabled) under <prefix>/<component>/paradox_<mac>/...:
//   alarm_control_panel per area, binary_sensor per enabled zone (motion / opening),
//   panel connection + trouble count diagnostics.
//   <base>/zone/<n>/open             true | false   (enabled zones only)
//   <base>/zone/<n>/json             JSON: name, open, alarm, bypassed, tamper, ...
//   <base>/event                     JSON per live panel event (not retained)
//   <base>/action/result             result of the last command (not retained)
//
// Commands (only if "allow control" is on):
//   <base>/area/<n>/set              full|away|AA | stay|home|SA | sleep|night|NA | disarm|off|D
//   <base>/zone/<n>/bypass           any payload: toggle bypass
//   <base>/pgm/<n>/set               on | off
//   <base>/beep                      any payload

#include <Arduino.h>

#include "paradox.h"

void mqttBegin(paradox::Panel& panel);
void mqttLoop();
void mqttPublishEvent(const paradox::Event& e);
const char* mqttStatus();
// Re-send every Home Assistant discovery config and all retained states now.
// Returns false if MQTT isn't connected.
bool mqttRepublish();  // "disabled", "connected", "connecting", ...
