#include "node_id.h"

#include "log.h"
#include "version.h"

#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr char kPrefsNs[] = "node";
static constexpr char kDefaultShort[] = "dmxwhip";

static char s_short[NodeId::kShortMax + 1] = {};
static char s_long[NodeId::kLongMax + 1] = {};
static bool s_loaded = false;

static void applyDefaults() {
  snprintf(s_short, sizeof(s_short), "%s", kDefaultShort);
  snprintf(s_long, sizeof(s_long), "dmxwhip v%s", kFirmwareVersion);
}

static bool validName(const char *p, size_t max) {
  if (!p || !p[0]) {
    return false;
  }
  size_t n = 0;
  for (; p[n]; ++n) {
    if (static_cast<uint8_t>(p[n]) < 0x20 || n >= max) {
      return false;
    }
  }
  return n > 0 && n <= max;
}

static void copyTrunc(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0) {
    return;
  }
  snprintf(dst, dstLen, "%s", src && src[0] ? src : "");
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  applyDefaults();
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  char longName[NodeId::kLongMax + 1] = {};
  char shortName[NodeId::kShortMax + 1] = {};
  const size_t gotLong = prefs.getString("long", longName, sizeof(longName));
  const size_t gotShort = prefs.getString("short", shortName, sizeof(shortName));
  prefs.end();
  if (gotLong > 0 && validName(longName, NodeId::kLongMax)) {
    copyTrunc(s_long, sizeof(s_long), longName);
    if (gotShort > 0 && validName(shortName, NodeId::kShortMax)) {
      copyTrunc(s_short, sizeof(s_short), shortName);
    } else {
      copyTrunc(s_short, sizeof(s_short), longName);
    }
  }
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("node", "nvs open failed");
    return;
  }
  prefs.putString("long", s_long);
  prefs.putString("short", s_short);
  prefs.end();
}

} // namespace

void NodeId::begin() {
  loadNvs();
  LOG_V("node", "name short=%s long=%s", s_short, s_long);
}

const char *NodeId::shortName() {
  loadNvs();
  return s_short;
}

const char *NodeId::longName() {
  loadNvs();
  return s_long;
}

bool NodeId::set(const char *longName, const char *shortName, bool save) {
  if (!validName(longName, kLongMax)) {
    return false;
  }
  char nextShort[kShortMax + 1] = {};
  if (shortName && shortName[0]) {
    if (!validName(shortName, kShortMax)) {
      return false;
    }
    copyTrunc(nextShort, sizeof(nextShort), shortName);
  } else {
    copyTrunc(nextShort, sizeof(nextShort), longName);
  }
  loadNvs();
  copyTrunc(s_long, sizeof(s_long), longName);
  copyTrunc(s_short, sizeof(s_short), nextShort);
  if (save) {
    saveNvs();
  }
  LOG_V("node", "name short=%s long=%s", s_short, s_long);
  return true;
}
