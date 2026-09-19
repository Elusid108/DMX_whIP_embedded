#include "live_cfg.h"

#include "live_input.h"
#include "log.h"
#include "pixel_map.h"

#include <Preferences.h>

namespace {

static constexpr char kPrefsNs[] = "live";
static constexpr uint8_t kFpsDefault = 40;
static constexpr uint8_t kBufDefault = 0;

static LiveProto s_proto = LiveProto::Auto;
static uint8_t s_fps = kFpsDefault;
static uint8_t s_buf = kBufDefault;
static bool s_park = true;
static bool s_loaded = false;

static bool validFps(uint8_t fps) {
  return fps == 20 || fps == 30 || fps == 40 || fps == 60;
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  const uint8_t proto = prefs.getUChar("proto", 0);
  if (proto <= static_cast<uint8_t>(LiveProto::Sacn)) {
    s_proto = static_cast<LiveProto>(proto);
  }
  const uint8_t fps = prefs.getUChar("fps", kFpsDefault);
  if (validFps(fps)) {
    s_fps = fps;
  }
  const uint8_t buf = prefs.getUChar("buf", kBufDefault);
  if (buf <= 3) {
    s_buf = buf;
  }
  s_park = prefs.getUChar("park", 1) != 0;
  prefs.end();
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("live", "nvs open failed");
    return;
  }
  prefs.putUChar("proto", static_cast<uint8_t>(s_proto));
  prefs.putUChar("fps", s_fps);
  prefs.putUChar("buf", s_buf);
  prefs.putUChar("park", s_park ? 1 : 0);
  prefs.end();
}

} // namespace

void LiveCfg::begin() {
  loadNvs();
  LOG_V("live", "cfg proto=%s fps=%u buf=%u park=%s", protoName(), s_fps, s_buf,
        parkName());
}

LiveProto LiveCfg::proto() {
  loadNvs();
  return s_proto;
}

uint8_t LiveCfg::fps() {
  loadNvs();
  return s_fps;
}

uint8_t LiveCfg::buf() {
  loadNvs();
  return s_buf;
}

bool LiveCfg::park() {
  loadNvs();
  return s_park;
}

uint32_t LiveCfg::showIntervalMs() {
  loadNvs();
  return 1000 / s_fps;
}

const char *LiveCfg::protoName() {
  loadNvs();
  switch (s_proto) {
  case LiveProto::ArtNet:
    return "artnet";
  case LiveProto::Sacn:
    return "sacn";
  case LiveProto::Auto:
  default:
    return "auto";
  }
}

const char *LiveCfg::parkName() {
  loadNvs();
  return s_park ? "yes" : "no";
}

bool LiveCfg::set(LiveProto proto, uint8_t fps, uint8_t buf, bool park,
                  bool save) {
  if (!validFps(fps) || buf > 3) {
    return false;
  }
  loadNvs();
  const bool protoChanged = proto != s_proto;
  const bool changed =
      protoChanged || fps != s_fps || buf != s_buf || park != s_park;
  s_proto = proto;
  s_fps = fps;
  s_buf = buf;
  s_park = park;
  if (save) {
    saveNvs();
  }
  if (protoChanged) {
    PixelMap::setAllProtos(static_cast<SegProto>(proto), save);
  }
  if (changed) {
    LOG_V("live", "cfg proto=%s fps=%u buf=%u park=%s", protoName(), s_fps,
          s_buf, parkName());
    LiveInput::applyCfg();
  }
  return true;
}
