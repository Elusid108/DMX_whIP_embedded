#include "show_net.h"

#include "log.h"

#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr char kNs[] = "show";
static ShowRole s_role = ShowRole::Standalone;
static char s_ssid[33];
static char s_pass[64];
static uint8_t s_ch = 6;

} // namespace

void ShowNet::begin() {
  Preferences prefs;
  if (!prefs.begin(kNs, true)) {
    return;
  }
  const uint8_t r = prefs.getUChar("role", 0);
  s_role = r <= 2 ? static_cast<ShowRole>(r) : ShowRole::Standalone;
  snprintf(s_ssid, sizeof(s_ssid), "%s", prefs.getString("ssid", "").c_str());
  snprintf(s_pass, sizeof(s_pass), "%s", prefs.getString("pass", "").c_str());
  s_ch = prefs.getUChar("ch", 6);
  prefs.end();
  if (s_ch < 1 || s_ch > 13) {
    s_ch = 6;
  }
  if (active()) {
    LOG_V("show", "role=%s ssid=%s ch=%u", roleName(), s_ssid, s_ch);
  }
}

ShowRole ShowNet::role() { return s_role; }

const char *ShowNet::roleName() {
  switch (s_role) {
  case ShowRole::Host:
    return "host";
  case ShowRole::Member:
    return "member";
  default:
    return "standalone";
  }
}

const char *ShowNet::ssid() { return s_ssid; }

const char *ShowNet::pass() { return s_pass; }

uint8_t ShowNet::channel() { return s_ch; }

bool ShowNet::active() { return s_role != ShowRole::Standalone && s_ssid[0]; }

bool ShowNet::isHost() { return active() && s_role == ShowRole::Host; }

bool ShowNet::isMember() { return active() && s_role == ShowRole::Member; }

bool ShowNet::parseRole(const char *s, ShowRole &out) {
  if (!s) {
    return false;
  }
  if (strcmp(s, "standalone") == 0) {
    out = ShowRole::Standalone;
  } else if (strcmp(s, "host") == 0) {
    out = ShowRole::Host;
  } else if (strcmp(s, "member") == 0) {
    out = ShowRole::Member;
  } else {
    return false;
  }
  return true;
}

bool ShowNet::save(ShowRole role, const char *ssid, const char *pass,
                   uint8_t channel) {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) {
    return false;
  }
  prefs.putUChar("role", static_cast<uint8_t>(role));
  prefs.putString("ssid", ssid ? ssid : "");
  prefs.putString("pass", pass ? pass : "");
  prefs.putUChar("ch", channel);
  prefs.end();
  s_role = role;
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
  snprintf(s_pass, sizeof(s_pass), "%s", pass ? pass : "");
  s_ch = channel;
  return true;
}
