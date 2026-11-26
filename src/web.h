#pragma once
#include "paradox.h"

// HTTP server (port 80, basic auth):
//   GET  /             control page
//   GET  /api/status   everything as JSON (panel, partitions, zones, PGMs, troubles, events)
//   POST /api/action   action=arm_stay|arm_sleep|arm_full|arm_stay_d|arm_sleep_d|disarm|
//                             disarm_all|bypass|pgm_on|pgm_off|beep
//                      partition=1|2  zone=1..32  pgm=1..16
//   POST /api/time     set the panel clock from NTP
void webBegin(paradox::Panel& panel);
void webLoop();
void buildStatusJson(String& out);
