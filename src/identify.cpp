#include "identify.h"

#include "led_ctrl.h"
#include "log.h"

#include <Arduino.h>
#include <FastLED.h>

namespace {

static constexpr uint8_t kIdentifyBriFloor = 64;

static bool s_on = false;
static bool s_boosted = false;
static uint32_t s_until = 0;

static void applyBoost() {
  const uint8_t saved = LedCtrl::get();
  const uint8_t show =
      saved > kIdentifyBriFloor ? saved : kIdentifyBriFloor;
  FastLED.setBrightness(show);
  s_boosted = true;
}

static void restoreBri() {
  if (!s_boosted) {
    return;
  }
  FastLED.setBrightness(LedCtrl::get());
  s_boosted = false;
}

} // namespace

void Identify::start(uint32_t ms) {
  if (ms < 200) {
    ms = 200;
  }
  if (ms > 15000) {
    ms = 15000;
  }
  s_on = true;
  s_until = millis() + ms;
  applyBoost();
  LOG_V("id", "start ms=%u bri=%u", static_cast<unsigned>(ms),
        FastLED.getBrightness());
}

void Identify::cancel() {
  if (s_on) {
    LOG_V("id", "cancel");
  }
  s_on = false;
  s_until = 0;
  restoreBri();
}

bool Identify::active() {
  if (!s_on) {
    restoreBri();
    return false;
  }
  if (static_cast<int32_t>(millis() - s_until) >= 0) {
    s_on = false;
    restoreBri();
    LOG_V("id", "done");
    return false;
  }
  return true;
}

void Identify::render(CRGB *leds, uint16_t count, uint32_t now) {
  if (!leds || !count) {
    return;
  }
  applyBoost();
  const bool flash = ((now / 150) % 2) == 0;
  const CRGB c = flash ? CRGB(0, 255, 255) : CRGB(255, 255, 255);
  fill_solid(leds, count, c);
}
