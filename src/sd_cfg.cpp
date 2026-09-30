#include "sd_cfg.h"

#include "platform.h"

#if WHIP_HAS_NET

void SdCfg::service() {}

#else

#include <Arduino.h>
#include <SD.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "led_bus.h"
#include "led_ctrl.h"
#include "led_test.h"
#include "log.h"
#include "node_id.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"

namespace {

static constexpr char kPath[] = "/whip.cfg";
static constexpr size_t kMaxBytes = 1024;

static bool s_applied = false;

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') {
    ++s;
  }
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
    --end;
  }
  *end = '\0';
  return s;
}

static bool parseUint(const char *s, long lo, long hi, long &out) {
  if (*s == '\0') {
    return false;
  }
  char *end = nullptr;
  const long v = strtol(s, &end, 10);
  if (*end != '\0' || v < lo || v > hi) {
    return false;
  }
  out = v;
  return true;
}

struct Pending {
  PixelMapSet led;
  bool ledChanged = false;
  bool havePlay = false;
  PlaySrc src = PlaySrc::Root;
  char path[kSdPathLen] = {0};
  bool haveLoop = false;
  PlayFileLoop loop = PlayFileLoop::All;
  char name[NodeId::kLongMax + 1] = {0};
  long bri = -1;
};

// False when the value does not fit the key.
static bool takeKey(const char *key, const char *val, Pending &p, bool &known) {
  known = true;
  long n = 0;
  if (strcmp(key, "name") == 0) {
    if (val[0] == '\0' || strlen(val) > NodeId::kLongMax) {
      return false;
    }
    strcpy(p.name, val);
    return true;
  }
  if (strcmp(key, "bri") == 0) {
    if (!parseUint(val, 0, 255, n)) {
      return false;
    }
    p.bri = n;
    p.led.brightness = static_cast<uint8_t>(n);
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "uni") == 0) {
    if (!parseUint(val, 0, 32767, n)) {
      return false;
    }
    p.led.startArtNetUniverse = static_cast<uint16_t>(n);
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "ch") == 0) {
    if (!parseUint(val, 1, 512, n)) {
      return false;
    }
    p.led.startChannel = static_cast<uint16_t>(n);
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "count") == 0) {
    if (!parseUint(val, 1, kLedCountMax, n)) {
      return false;
    }
    p.led.pixelCount = static_cast<uint16_t>(n);
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "data") == 0 || strcmp(key, "clk") == 0) {
    if (!parseUint(val, 0, 254, n)) {
      return false;
    }
    if (key[0] == 'd') {
      p.led.dataGpio = static_cast<uint8_t>(n);
    } else {
      p.led.clockGpio = static_cast<uint8_t>(n);
    }
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "chip") == 0) {
    if (!PixelMap::parseChipset(val, p.led.chipset)) {
      return false;
    }
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "order") == 0) {
    if (!PixelMap::parseOrder(val, p.led.colorOrder, p.led.white, p.led.cct)) {
      return false;
    }
    p.ledChanged = true;
    return true;
  }
  if (strcmp(key, "play") == 0) {
    if (strcmp(val, "root") == 0) {
      p.src = PlaySrc::Root;
      strcpy(p.path, "/");
    } else if (strcmp(val, "none") == 0) {
      p.src = PlaySrc::None;
      p.path[0] = '\0';
    } else if (strncmp(val, "file:", 5) == 0 || strncmp(val, "folder:", 7) == 0) {
      const char *path = strchr(val, ':') + 1;
      if (path[0] != '/' || strlen(path) >= sizeof(p.path)) {
        return false;
      }
      p.src = val[1] == 'i' ? PlaySrc::File : PlaySrc::Folder;
      strcpy(p.path, path);
    } else {
      return false;
    }
    p.havePlay = true;
    return true;
  }
  if (strcmp(key, "loop") == 0) {
    if (strcmp(val, "all") == 0) {
      p.loop = PlayFileLoop::All;
    } else if (strcmp(val, "one") == 0) {
      p.loop = PlayFileLoop::One;
    } else {
      return false;
    }
    p.haveLoop = true;
    return true;
  }
  known = false;
  return true;
}

// Text of whip.cfg, NUL-terminated. False when there is no such file.
static bool readFile(char *buf, size_t cap) {
  if (!SdInfo::lock(1000)) {
    return false;
  }
  bool ok = false;
  if (SD.exists(kPath)) {
    File f = SD.open(kPath, FILE_READ);
    if (f) {
      const int got = f.read(reinterpret_cast<uint8_t *>(buf), cap - 1);
      f.close();
      if (got >= 0) {
        buf[got] = '\0';
        ok = true;
      }
    }
  }
  SdInfo::unlock();
  return ok;
}

static void apply() {
  static char text[kMaxBytes];
  if (!readFile(text, sizeof(text))) {
    LOG_V("cfg", "no whip.cfg on the card");
    return;
  }

  Pending p;
  const PixelMapCfg &cur = PixelMap::cfg();
  p.led.proto = cur.proto;
  p.led.chipset = cur.chipset;
  memcpy(p.led.colorOrder, cur.colorOrder, sizeof(p.led.colorOrder));
  p.led.dataGpio = cur.dataGpio;
  p.led.clockGpio = cur.clockGpio;
  p.led.pixelCount = cur.pixelCount;
  p.led.startArtNetUniverse = cur.startArtNetUniverse;
  p.led.startChannel = cur.startChannel;
  p.led.white = cur.white;
  p.led.cct = cur.cct;
  p.led.brightness = cur.brightness;

  uint8_t used = 0;
  char *save = nullptr;
  for (char *line = strtok_r(text, "\n", &save); line != nullptr;
       line = strtok_r(nullptr, "\n", &save)) {
    char *hash = strchr(line, '#');
    if (hash != nullptr) {
      *hash = '\0';
    }
    line = trim(line);
    if (line[0] == '\0') {
      continue;
    }
    char *eq = strchr(line, '=');
    if (eq == nullptr) {
      LOG_C("cfg", "whip.cfg: no '=' in \"%s\"", line);
      continue;
    }
    *eq = '\0';
    char *key = trim(line);
    char *val = trim(eq + 1);
    for (char *c = key; *c; ++c) {
      *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
    }
    bool known = false;
    if (!takeKey(key, val, p, known)) {
      LOG_C("cfg", "whip.cfg: bad value %s=%s", key, val);
    } else if (!known) {
      LOG_C("cfg", "whip.cfg: unknown key %s", key);
    } else {
      ++used;
    }
  }

  if (p.name[0] != '\0' && !NodeId::set(p.name, nullptr, false)) {
    LOG_C("cfg", "whip.cfg: name not accepted");
  }
  if (p.bri >= 0) {
    LedCtrl::set(static_cast<uint8_t>(p.bri), false);
  }
  bool reload = false;
  if (p.ledChanged) {
    if (PixelMap::set(p.led, false)) {
      LedBus::requestApply();
      LedTest::cancel();
      reload = true;
    } else {
      LOG_C("cfg", "whip.cfg: LED settings not accepted (pin, count or order)");
    }
  }
  if (p.havePlay || p.haveLoop) {
    const PlaySrc src = p.havePlay ? p.src : PlayCfg::src();
    const char *path = p.havePlay ? p.path : PlayCfg::path();
    const PlayFileLoop loop = p.haveLoop ? p.loop : PlayCfg::fileLoop();
    if (PlayCfg::set(src, path, loop, PlayCfg::folderRep(), PlayCfg::folderN(),
                     false, false)) {
      reload = true;
    } else {
      LOG_C("cfg", "whip.cfg: play not accepted");
    }
  }
  if (reload) {
    Playback::reload();
  }
  LOG_V("cfg", "whip.cfg applied (%u settings)", used);
}

} // namespace

void SdCfg::service() {
  if (!SdInfo::ok()) {
    s_applied = false;
    return;
  }
  if (s_applied) {
    return;
  }
  s_applied = true;
  apply();
}

#endif
