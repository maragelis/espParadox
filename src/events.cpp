#include "events.h"

#include "zones.h"

using paradox::Event;
using paradox::Panel;

namespace {

enum SubKind : uint8_t { kNone, kZone, kUser, kRemote, kModule, kList };

struct GroupDef {
  uint8_t group;
  const char* name;
  SubKind kind;
  const char* const* list;
  uint8_t listLen;
};

#define LIST(a) a, (uint8_t)(sizeof(a) / sizeof(a[0]))

const char* const kPartitionStatus[] = {
    nullptr, nullptr, "Silent alarm", "Buzzer alarm", "Steady alarm", "Pulsed alarm",
    "Strobe", "Alarm stopped", "Squawk ON (arm)", "Squawk OFF (disarm)", "Ground start",
    "Partition disarmed", "Partition armed", "Entry delay started", "Exit delay started",
    "Pre-alarm delay", "Report confirmation"};

const char* const kBellStatus[] = {"Bell OFF", "Bell ON", "Bell squawk arm", "Bell squawk disarm"};

const char* const kNonReportable[] = {
    "Telephone line trouble", "ENTER/CLEAR/POWER key pressed", nullptr, "Arm in Stay mode",
    "Arm in Sleep mode", "Arm in Force mode", "Full arm while Stay armed",
    "PC fail to communicate", "Utility key 1 (1+2)", "Utility key 2 (2+3)",
    "Utility key 3 (4+5)", "Utility key 4 (5+6)", "Utility key 5 (7+8)", "Utility key 6 (8+9)",
    "Tamper generated alarm", "Supervision loss generated alarm", nullptr, nullptr, nullptr,
    nullptr, "Full arm while Sleep armed", "Firmware upgrade", nullptr, "StayD activated",
    "StayD deactivated", "IP registration status change", "GPRS registration status change"};

const char* const kSoftwareAccess[] = {
    "Invalid source ID", "WinLoad/BabyWare direct", "WinLoad/BabyWare via IP",
    "WinLoad/BabyWare via GSM", "WinLoad/BabyWare via modem", nullptr, nullptr, nullptr,
    nullptr, "IP100 direct", "VDMP3 direct", "VDMP3 via GSM", "Remote access", "SMS via GSM"};

const char* const kBusModule[] = {"Bus module added", "Bus module removed",
                                  "2-way RF module comm lost", "2-way RF module comm restored"};

const char* const kSpecialArm[] = {"Auto-arming",     "Late to close",        "No-movement arming",
                                   "Partial arming",  "Quick arming",         "Arming via WinLoad",
                                   "Keyswitch arming"};

const char* const kSpecialDisarm[] = {"Auto-arm cancelled",
                                      "Disarm via WinLoad",
                                      "Disarm via WinLoad after alarm",
                                      "Alarm cancelled via WinLoad",
                                      "Paramedic alarm cancelled",
                                      "Keyswitch disarm",
                                      "Keyswitch disarm after alarm",
                                      "Alarm cancelled with keyswitch"};

const char* const kSpecialAlarm[] = {"Panic (non-medical)", "Panic (medical)", "Panic (fire)",
                                     "Recent closing",      "Global shutdown", "Duress alarm",
                                     "Keypad lockout"};

const char* const kTrouble[] = {"Telephone line",
                                "AC failure",
                                "Battery failure",
                                "Auxiliary current overload",
                                "Bell current overload",
                                "Bell disconnected",
                                "Clock loss",
                                "Fire loop trouble",
                                "Fail to communicate (tel #1)",
                                "Fail to communicate (tel #2)",
                                "Fail to communicate (pager)",
                                "Fail to communicate (voice)",
                                "RF jamming",
                                "GSM RF jamming",
                                "GSM no service",
                                "GSM supervision lost",
                                "IP receiver 1 comm fail (GPRS)",
                                "IP receiver 2 comm fail (GPRS)",
                                "IP module no service",
                                "IP module supervision lost",
                                "IP receiver 1 comm fail (IP)",
                                "IP receiver 2 comm fail (IP)"};

const char* const kModuleTrouble[] = {"Bus communication", "Tamper", "AC failure", "Battery"};

const char* const kSpecial[] = {"System power up",
                                "Test report",
                                "Software log on",
                                "Software log off",
                                "Installer entered programming",
                                "Installer exited programming",
                                "Maintenance entered programming",
                                "Maintenance exited programming",
                                "Closing delinquency delay elapsed"};

const GroupDef kGroups[] = {
    {0, "Zone closed", kZone, nullptr, 0},
    {1, "Zone open", kZone, nullptr, 0},
    {2, "Partition", kList, LIST(kPartitionStatus)},
    {3, "Bell", kList, LIST(kBellStatus)},
    {6, "Status", kList, LIST(kNonReportable)},
    {8, "Remote button B", kRemote, nullptr, 0},
    {9, "Remote button C", kRemote, nullptr, 0},
    {10, "Remote button D", kRemote, nullptr, 0},
    {11, "Remote button E", kRemote, nullptr, 0},
    {12, "Cold start wireless zone", kZone, nullptr, 0},
    {13, "Cold start wireless module", kModule, nullptr, 0},
    {14, "Bypass programming", kUser, nullptr, 0},
    {15, "User activated PGM", kUser, nullptr, 0},
    {16, "Wireless smoke maintenance", kZone, nullptr, 0},
    {17, "Delayed alarm transmission", kZone, nullptr, 0},
    {18, "Weak signal 1", kZone, nullptr, 0},
    {19, "Weak signal 2", kZone, nullptr, 0},
    {20, "Weak signal 3", kZone, nullptr, 0},
    {21, "Weak signal 4", kZone, nullptr, 0},
    {22, "Remote button 5", kRemote, nullptr, 0},
    {23, "Remote button 6", kRemote, nullptr, 0},
    {24, "Fire delay started", kZone, nullptr, 0},
    {26, "Software access", kList, LIST(kSoftwareAccess)},
    {27, "Bus module", kList, LIST(kBusModule)},
    {28, "StayD pass", kZone, nullptr, 0},
    {29, "Armed by", kUser, nullptr, 0},
    {30, "Special arming", kList, LIST(kSpecialArm)},
    {31, "Disarmed by", kUser, nullptr, 0},
    {32, "Disarmed after alarm by", kUser, nullptr, 0},
    {33, "Alarm cancelled by", kUser, nullptr, 0},
    {34, "Special disarming", kList, LIST(kSpecialDisarm)},
    {35, "Zone bypassed", kZone, nullptr, 0},
    {36, "ZONE ALARM", kZone, nullptr, 0},
    {37, "FIRE ALARM", kZone, nullptr, 0},
    {38, "Zone alarm restore", kZone, nullptr, 0},
    {39, "Fire alarm restore", kZone, nullptr, 0},
    {40, "SPECIAL ALARM", kList, LIST(kSpecialAlarm)},
    {41, "Zone shutdown", kZone, nullptr, 0},
    {42, "Zone tamper", kZone, nullptr, 0},
    {43, "Zone tamper restore", kZone, nullptr, 0},
    {44, "New trouble", kList, LIST(kTrouble)},
    {45, "Trouble restored", kList, LIST(kTrouble)},
    {46, "Module trouble", kList, LIST(kModuleTrouble)},
    {47, "Module trouble restored", kList, LIST(kModuleTrouble)},
    {48, "Special", kList, LIST(kSpecial)},
    {49, "Zone low battery", kZone, nullptr, 0},
    {50, "Zone battery restored", kZone, nullptr, 0},
    {51, "Zone supervision trouble", kZone, nullptr, 0},
    {52, "Zone supervision restored", kZone, nullptr, 0},
    {53, "Wireless module supervision trouble", kModule, nullptr, 0},
    {54, "Wireless module supervision restored", kModule, nullptr, 0},
    {55, "Wireless module tamper", kModule, nullptr, 0},
    {56, "Wireless module tamper restored", kModule, nullptr, 0},
    {57, "Paramedic alarm by", kUser, nullptr, 0},
    {58, "Zone forced", kZone, nullptr, 0},
    {59, "Zone included", kZone, nullptr, 0},
    {64, "System status", kNone, nullptr, 0},
};

const GroupDef* findGroup(uint8_t g) {
  for (const auto& d : kGroups)
    if (d.group == g) return &d;
  return nullptr;
}

struct TroubleBit {
  uint8_t byteIdx;  // 0..4 = status page 0 bytes 4..8
  uint8_t bit;
  const char* name;
};

const TroubleBit kTroubleBits[] = {
    {0, 7, "Timer loss"},
    {0, 6, "Fire loop"},
    {0, 5, "Module tamper"},
    {0, 4, "Zone tamper"},
    {0, 3, "Communication"},
    {0, 2, "Bell"},
    {0, 1, "Power"},
    {0, 0, "RF transmitter low battery"},
    {1, 7, "RF interference"},
    {1, 1, "Module supervision"},
    {1, 0, "Zone supervision"},
    {2, 6, "Wireless repeater battery"},
    {2, 5, "Wireless repeater AC loss"},
    {2, 4, "Wireless keypad battery"},
    {2, 3, "Wireless keypad AC loss"},
    {2, 2, "Auxiliary output overload"},
    {2, 1, "AC failure"},
    {2, 0, "No / low battery"},
    {3, 1, "Bell output overload"},
    {3, 0, "Bell output disconnected"},
    {4, 5, "Computer fail to communicate"},
    {4, 4, "Voice fail to communicate"},
    {4, 3, "Pager fail to communicate"},
    {4, 2, "Central 2 fail to communicate"},
    {4, 1, "Central 1 fail to communicate"},
    {4, 0, "Telephone line"},
};

}  // namespace

const char* eventUserName(const Panel& panel, const Event& e) {
  const GroupDef* g = findGroup(e.group);
  if (!g || g->kind != kUser || e.sub < 1 || e.sub > paradox::kUsers) return nullptr;
  return panel.userLabel(e.sub - 1);
}

void describeEvent(const Panel& panel, const Event& e, char* out, size_t n) {
  const GroupDef* g = findGroup(e.group);
  if (!g) {
    snprintf(out, n, "Event %u/%u", e.group, e.sub);
    return;
  }
  switch (g->kind) {
    case kZone:
      if (e.sub >= 1 && e.sub <= paradox::kZones)
        snprintf(out, n, "%s: %s", g->name, zoneName(e.sub - 1));
      else
        snprintf(out, n, "%s: zone %u", g->name, e.sub);
      break;
    case kUser: {
      const char* who = eventUserName(panel, e);
      if (who) snprintf(out, n, "%s %s", g->name, who);
      else snprintf(out, n, "%s user %u", g->name, e.sub);
      break;
    }
    case kRemote:
      snprintf(out, n, "%s (remote %u)", g->name, e.sub);
      break;
    case kModule:
      if (e.sub >= 1 && e.sub <= 16)
        snprintf(out, n, "%s: %s", g->name, panel.pgm(e.sub - 1).label);
      else if (e.sub >= 17 && e.sub <= 18)
        snprintf(out, n, "%s: repeater %u", g->name, e.sub - 16);
      else if (e.sub >= 19 && e.sub <= 22)
        snprintf(out, n, "%s: keypad %u", g->name, e.sub - 18);
      else
        snprintf(out, n, "%s: %u", g->name, e.sub);
      break;
    case kList: {
      const char* s = (e.sub < g->listLen) ? g->list[e.sub] : nullptr;
      if (e.group == 45 && e.sub == 0) s = "Telephone line";
      if (s)
        snprintf(out, n, "%s: %s", g->name, s);
      else
        snprintf(out, n, "%s: %u", g->name, e.sub);
      break;
    }
    case kNone:
      snprintf(out, n, "%s %u", g->name, e.sub);
      break;
  }
}

int listTroubles(const paradox::SystemStatus& s, const char** out, int max) {
  int count = 0;
  for (const auto& t : kTroubleBits) {
    if (count >= max) break;
    if (s.trouble[t.byteIdx] & (1 << t.bit)) out[count++] = t.name;
  }
  return count;
}
