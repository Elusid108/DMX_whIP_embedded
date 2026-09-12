#include <Arduino.h>
#include <FastLED.h>

#include "board_matrix.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "sd_info.h"
#include "version.h"
#include "wifi_setup.h"

static CRGB leds[kLedCount];

static void renderLive(const uint8_t *d, uint16_t len) {
  for (uint16_t p = 0; p < kLedCount; ++p) {
    const uint16_t i = static_cast<uint16_t>(p * 3);
    if (i + 2 < len) {
      leds[p] = CRGB(d[i], d[i + 1], d[i + 2]);
    } else {
      leds[p] = CRGB::Black;
    }
  }
}

void setup() {
  Log::begin(115200, kLogLevelDefault);
  LOG_V("boot", "ESP32-S3-Matrix v%s", kFirmwareVersion);
  LOG_V("log", "level=%u (0=off 1=critical 2=verbose)",
        static_cast<unsigned>(Log::level()));
  WifiSetup::begin();
  LiveCfg::begin();
  LiveInput::begin();
  SdInfo::begin();

  FastLED.addLeds<WS2812B, kLedPin, GRB>(leds, kLedCount);
  LedCtrl::begin();
  FastLED.clear(true);
  LOG_V("led", "init pin=%u count=%u brightness=%u", kLedPin, kLedCount,
        LedCtrl::get());
}

void loop() {
  Log::service();
  LiveInput::service();
  WifiSetup::service();
  SdInfo::service();

  static uint32_t lastShow = 0;
  const uint32_t now = millis();
  if (now - lastShow < LiveCfg::showIntervalMs()) {
    yield();
    return;
  }
  lastShow = now;

  if (LiveInput::active()) {
    const uint8_t *d = nullptr;
    uint16_t len = 0;
    if (LiveInput::pop(d, len)) {
      renderLive(d, len);
    }
  } else {
    FastLED.clear();
  }
  FastLED.show();
}
