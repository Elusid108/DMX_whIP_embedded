#include <Arduino.h>
#include <FastLED.h>

#include <cstring>

#include "board_matrix.h"
#include "board_profile.h"
#include "identify.h"
#include "led_bus.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "node_id.h"
#include "playback.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "sd_info.h"
#include "sync.h"
#include "version.h"
#include "wifi_setup.h"

static bool s_livePreemptedPlay = false;

static uint8_t s_frame[kPlayMaxPayload];

static void renderPacked(const uint8_t *d, uint16_t len) {
  LedBus::clear();
  const uint16_t n = PixelMap::outputPixelCount(0);
  const uint8_t ch = PixelMap::cfg().channelsPerPixel;
  const uint16_t off = PixelMap::outputPixelOffset(0);
  for (uint16_t p = 0; p < n; ++p) {
    const uint16_t i = static_cast<uint16_t>(p * ch);
    if (ch >= 3 && static_cast<uint16_t>(i + ch) <= len) {
      LedBus::setPacked(static_cast<uint16_t>(off + p), d + i);
    } else {
      LedBus::setRgb(static_cast<uint16_t>(off + p), 0, 0, 0);
    }
  }
}

void setup() {
  Log::begin(115200, kLogLevelDefault);
  LOG_V("boot", "ESP32-S3-Matrix v%s", kFirmwareVersion);
  LOG_V("log", "level=%u (0=off 1=critical 2=verbose)",
        static_cast<unsigned>(Log::level()));
  NodeId::begin();
  BoardProfile::begin();
  WifiSetup::begin();
  LiveCfg::begin();
  LiveInput::begin();
  SdInfo::begin();
  PixelMap::begin();
  PlayCfg::begin();
  Playback::begin();
  Sync::begin();

  LedBus::begin();
  LedCtrl::begin();
  LedBus::clear();
  LedBus::show();
  LOG_V("led", "init outs=%u segs=%u pin=%u count=%u brightness=%u",
        PixelMap::outputCount(), PixelMap::segmentCount(),
        PixelMap::cfg().dataGpio, PixelMap::totalPixels(), LedCtrl::get());
}

void loop() {
  Log::service();
  LedBus::service();
  LiveInput::service();
  Sync::service();

  const bool live = LiveInput::active();
  if (live) {
    Identify::cancel();
    if (Playback::running()) {
      LOG_V("main", "live preempts play");
      s_livePreemptedPlay = true;
    }
    Playback::stop();
  } else if (Identify::active()) {
    if (Playback::playing()) {
      Playback::pause();
    }
  } else {
    Playback::service();
    if (Playback::parked()) {
      // Stay black; NVS playlist is unchanged until the next /play.
    } else if (Playback::hasFile()) {
      if (Sync::cueFollow()) {
        if (Sync::cuePlaying()) {
          Playback::play();
        } else {
          Playback::pause();
          if (!Playback::running()) {
            Playback::start();
          }
        }
      } else if (!Playback::running()) {
        if (s_livePreemptedPlay) {
          LOG_V("main", "play resume after silence");
          s_livePreemptedPlay = false;
        }
        Playback::start();
        Playback::play();
      } else if (!Playback::playing()) {
        Playback::play();
      }
    }
  }

  WifiSetup::service();
  SdInfo::service();

  static uint32_t lastShow = 0;
  const uint32_t now = millis();
  const bool fpsDue = (now - lastShow >= LiveCfg::showIntervalMs());
  const bool syncLive = live && Sync::liveSyncActive();
  const bool liveFence = syncLive && Sync::hasLiveFence();
  const bool cuePulse = !live && Sync::hasCuePulse();

  if (!fpsDue && !liveFence && !cuePulse) {
    yield();
    return;
  }
  if (liveFence) {
    Sync::takeLiveFence();
  }
  if (cuePulse) {
    Sync::takeCuePulse();
  }
  if (fpsDue) {
    lastShow = now;
  }

  if (live) {
    if (syncLive) {
      if (liveFence) {
        LiveInput::renderLeds();
      }
    } else {
      LiveInput::renderLeds();
    }
  } else if (Identify::active()) {
    Identify::render(now);
  } else if (Playback::hasFile()) {
    if (Sync::cueFollow()) {
      if (cuePulse && Sync::cuePlaying() &&
          (Sync::cueHasTime() || Sync::cueHasFrame())) {
        memset(s_frame, 0, sizeof(s_frame));
        if (Playback::catchTick(s_frame, sizeof(s_frame), Sync::cueTargetMs(),
                                Sync::cueTargetFrame(), Sync::cueHasTime(),
                                Sync::cueHasFrame())) {
          renderPacked(s_frame, static_cast<uint16_t>(sizeof(s_frame)));
        }
      }
    } else if (fpsDue) {
      uint32_t t_us = 0;
      if (Playback::peek(t_us)) {
        memset(s_frame, 0, sizeof(s_frame));
        if (Playback::copyFrame(s_frame, sizeof(s_frame))) {
          renderPacked(s_frame, static_cast<uint16_t>(sizeof(s_frame)));
        }
      }
    }
  } else if (fpsDue) {
    LedBus::clear();
  }
  LedBus::show();
}
