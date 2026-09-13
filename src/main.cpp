#include <Arduino.h>
#include <FastLED.h>

#include <cstring>

#include "board_matrix.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "playback.h"
#include "pixel_map.h"
#include "sd_info.h"
#include "version.h"
#include "wifi_setup.h"

static CRGB leds[kLedCount];
static bool s_livePreemptedPlay = false;

static void renderRgb(const uint8_t *d, uint16_t len) {
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
  PixelMap::begin();
  Playback::begin();

  FastLED.addLeds<WS2812B, kLedPin, GRB>(leds, kLedCount);
  LedCtrl::begin();
  FastLED.clear(true);
  LOG_V("led", "init pin=%u count=%u brightness=%u", kLedPin, kLedCount,
        LedCtrl::get());
}

void loop() {
  Log::service();
  LiveInput::service();

  const bool live = LiveInput::active();
  if (live) {
    if (Playback::running()) {
      LOG_V("main", "live preempts play");
      s_livePreemptedPlay = true;
    }
    Playback::stop();
  } else {
    Playback::service();
    if (Playback::hasFile() && !Playback::running()) {
      if (s_livePreemptedPlay) {
        LOG_V("main", "play resume after silence");
        s_livePreemptedPlay = false;
      }
      Playback::start();
    }
  }

  WifiSetup::service();
  SdInfo::service();

  static uint32_t lastShow = 0;
  const uint32_t now = millis();
  if (now - lastShow < LiveCfg::showIntervalMs()) {
    yield();
    return;
  }
  lastShow = now;

  if (live) {
    const uint8_t *d = nullptr;
    uint16_t len = 0;
    if (LiveInput::pop(d, len)) {
      renderRgb(d, len);
    }
  } else if (Playback::hasFile()) {
    uint32_t t_us = 0;
    if (Playback::peek(t_us)) {
      uint8_t rgb[kPlayMaxPayload];
      memset(rgb, 0, sizeof(rgb));
      if (Playback::copyFrame(rgb, sizeof(rgb))) {
        renderRgb(rgb, static_cast<uint16_t>(sizeof(rgb)));
      }
    }
  } else {
    FastLED.clear();
  }
  FastLED.show();
}
