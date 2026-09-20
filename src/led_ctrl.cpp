#include "led_ctrl.h"

#include "boards/select.h"
#include "log.h"

#include <FastLED.h>
#include <Preferences.h>

namespace {

static constexpr char kPrefsNs[] = "led";
static uint8_t s_bri = kBrightnessDefault;
static bool s_loaded = false;

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  s_bri = prefs.getUChar("bri", kBrightnessDefault);
  prefs.end();
}

static void saveNvs(uint8_t v) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("led", "nvs open failed");
    return;
  }
  prefs.putUChar("bri", v);
  prefs.end();
}

} // namespace

void LedCtrl::begin() {
  loadNvs();
  FastLED.setBrightness(s_bri);
  LOG_V("led", "brightness=%u", s_bri);
}

uint8_t LedCtrl::get() {
  loadNvs();
  return s_bri;
}

void LedCtrl::set(uint8_t v, bool save) {
  loadNvs();
  if (v != s_bri) {
    s_bri = v;
    FastLED.setBrightness(s_bri);
    LOG_V("led", "brightness=%u", s_bri);
  }
  if (save) {
    saveNvs(s_bri);
  }
}
