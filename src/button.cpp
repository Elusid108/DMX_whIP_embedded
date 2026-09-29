#include "button.h"

#include <Arduino.h>

#include "board_profile.h"
#include "live_input.h"
#include "log.h"
#include "playback.h"
#include "sync.h"

namespace {

static constexpr uint32_t kDebounceMs = 30;
// A pin that floats or is held low at boot does not fire at once.
static constexpr uint32_t kArmMs = 500;

static uint8_t s_pin = kGpioUnset;
static bool s_down = false;
static bool s_raw = false;
static uint32_t s_changedAt = 0;
static uint32_t s_armAt = 0;

static void press() {
  if (LiveInput::active() && !Playback::hold()) {
    LOG_V("btn", "ignored (live)");
    return;
  }
  if (!Playback::hasFile()) {
    LOG_V("btn", "ignored (no show)");
    return;
  }
  if (Playback::userPaused()) {
    if (!Sync::noteLocalResume()) {
      Playback::userResume();
    }
    LOG_V("btn", "resume");
  } else {
    if (!Sync::noteLocalPause()) {
      Playback::userPause();
    }
    LOG_V("btn", "pause");
  }
}

} // namespace

void Button::begin() {
  s_pin = BoardProfile::buttonPin();
  s_down = false;
  s_raw = false;
  s_changedAt = millis();
  s_armAt = millis() + kArmMs;
  if (s_pin == kGpioUnset) {
    return;
  }
  pinMode(s_pin, INPUT_PULLUP);
  LOG_V("btn", "play/pause on gpio %u", s_pin);
}

void Button::service() {
  if (s_pin == kGpioUnset) {
    return;
  }
  const uint32_t now = millis();
  const bool raw = digitalRead(s_pin) == LOW;
  if (raw != s_raw) {
    s_raw = raw;
    s_changedAt = now;
    return;
  }
  if (raw == s_down || now - s_changedAt < kDebounceMs) {
    return;
  }
  s_down = raw;
  if (s_down && static_cast<int32_t>(now - s_armAt) >= 0) {
    press();
  }
}
