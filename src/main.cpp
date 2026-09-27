#include <Arduino.h>
#include <FastLED.h>

#include <cstring>

#include "board_profile.h"
#include "fixture.h"
#include "identify.h"
#include "led_bus.h"
#include "led_test.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "node_id.h"
#include "ota.h"
#include "playback.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "sd_info.h"
#include "sync.h"
#include "version.h"
#include "wifi_setup.h"

static bool s_livePreemptedPlay = false;

static uint8_t s_frame[kPlayMaxPayload];

// d stays untouched (a held frame is re-rendered when the fixture animates).
static void renderPacked(const uint8_t *d, uint16_t len) {
  LedBus::clear();
  const uint16_t n = PixelMap::outputPixelCount(0);
  const uint8_t ch = PixelMap::cfg().channelsPerPixel;
  const uint16_t off = PixelMap::outputPixelOffset(0);
  // SD shows are looks: only Dim mode's overlay applies to them.
  const bool fx = Fixture::beginFrame(millis()) == FixDrive::Look;
  uint8_t px[kMaxChannelsPerPixel];
  for (uint16_t p = 0; p < n; ++p) {
    const uint16_t i = static_cast<uint16_t>(p * ch);
    if (ch >= 3 && static_cast<uint16_t>(i + ch) <= len) {
      if (fx) {
        memcpy(px, d + i, ch);
        Fixture::applyLook(static_cast<uint16_t>(off + p), px, ch);
        LedBus::setPacked(static_cast<uint16_t>(off + p), px);
      } else {
        LedBus::setPacked(static_cast<uint16_t>(off + p), d + i);
      }
    } else {
      LedBus::setRgb(static_cast<uint16_t>(off + p), 0, 0, 0);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxBufferSize(4096);
  Log::begin(115200, kLogLevelDefault);
  LOG_V("boot", "%s v%s", BoardProfile::id(), kFirmwareVersion);
  LOG_V("log", "level=%u (0=off 1=critical 2=verbose)",
        static_cast<unsigned>(Log::level()));
  Ota::bootGuard();
  NodeId::begin();
  BoardProfile::begin();
  WifiSetup::begin();
  LiveCfg::begin();
  LiveInput::begin();
  SdInfo::begin();
  PixelMap::begin();
  Fixture::begin();
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
  Fixture::service();

  const bool live = LiveInput::active() && !Playback::hold();
  if (live) {
    Identify::cancel();
    LedTest::cancel();
    Fixture::cancelLocate();
    if (Playback::running()) {
      LOG_V("main", "live preempts play");
      s_livePreemptedPlay = true;
    }
    Playback::stop();
  } else if (Identify::active()) {
    if (Playback::playing()) {
      Playback::pause();
    }
  } else if (LedTest::active() || Fixture::locating()) {
    if (Playback::playing()) {
      Playback::pause();
    }
  } else {
    Playback::service();
    if (Playback::parked()) {
      // Stay black; NVS playlist is unchanged until the next /play.
    } else if (Playback::userPaused()) {
      // Hold last pixels; do not auto-resume.
    } else if (Playback::hasFile()) {
      if (Sync::inCue() || Sync::waitingForCue()) {
        // The group schedule (or the wait for one) decides what shows; keep
        // the reader bound and filling.
        if (!Playback::running()) {
          Playback::start();
        }
        if (Sync::inCue() && !Playback::playing()) {
          Playback::play();
        }
      } else if (!Playback::running()) {
        if (LiveCfg::loss() == LiveLoss::Play) {
          if (s_livePreemptedPlay) {
            LOG_V("main", "play resume after silence");
            s_livePreemptedPlay = false;
          }
          Playback::start();
          Playback::play();
        }
      } else if (!Playback::playing()) {
        Playback::play();
      }
    }
  }

  WifiSetup::service();
  SdInfo::service();

  // Render. LedBus::show() only when pixels changed (plus a 1 s keepalive so
  // a glitched strip heals); a held playback frame is not re-sent every tick.
  enum class Mode : uint8_t { Live, Ident, Test, Locate, Play, Hold, Black };
  static Mode prevMode = Mode::Black;
  static uint32_t lastShow = 0;
  static bool haveFrame = false;
  const uint32_t now = millis();
  const bool fpsDue = (now - lastShow >= LiveCfg::showIntervalMs());
  const bool syncLive = live && Sync::liveSyncActive();
  const bool liveFence = syncLive && Sync::hasLiveFence();
  if (liveFence) {
    Sync::takeLiveFence();
  }

  Mode mode = Mode::Black;
  if (live) {
    mode = Mode::Live;
  } else if (Identify::active()) {
    mode = Mode::Ident;
  } else if (LedTest::active()) {
    mode = Mode::Test;
  } else if (Fixture::locating()) {
    mode = Mode::Locate;
  } else if (Playback::hasFile() && !Playback::userPaused() &&
             (LiveCfg::loss() == LiveLoss::Play || Playback::playing() ||
              Sync::inCue())) {
    mode = Mode::Play;
  } else if (Playback::userPaused() || LiveCfg::loss() == LiveLoss::Hold) {
    mode = Mode::Hold;
  }
  const bool entered = mode != prevMode;
  prevMode = mode;
  // A patched fixture (strobe, dimmer moves) changes pixels between content
  // frames: re-render every show tick while its console is heard.
  const bool fxTick = fpsDue && Fixture::animating();

  bool drew = false;
  switch (mode) {
  case Mode::Live:
    if (syncLive ? liveFence : fpsDue) {
      drew = LiveInput::renderLeds(fxTick);
    } else if (fxTick) {
      drew = LiveInput::renderLeds(true);
    }
    break;
  case Mode::Ident:
    if (fpsDue) {
      Identify::render(now);
      drew = true;
    }
    break;
  case Mode::Test:
    if (fpsDue) {
      LedTest::render(now);
      drew = true;
    }
    break;
  case Mode::Locate:
    if (fpsDue) {
      Fixture::renderLocate();
      drew = true;
    }
    break;
  case Mode::Play: {
    // A group schedule holds before its start and while paused; a slice
    // waiting for its group's cue holds its last pixels.
    const bool rolling = !Sync::waitingForCue();
    if (rolling && fpsDue && Playback::frameDue(now)) {
      memset(s_frame, 0, sizeof(s_frame));
      if (Playback::renderDue(s_frame, sizeof(s_frame), now)) {
        haveFrame = true;
        renderPacked(s_frame, static_cast<uint16_t>(sizeof(s_frame)));
        drew = true;
      }
    } else if (rolling && fpsDue && Playback::available() == 0) {
      // Flags the underrun and pauses the show clock.
      Playback::renderDue(s_frame, sizeof(s_frame), now);
    }
    if (!drew && haveFrame && (entered || fxTick)) {
      renderPacked(s_frame, static_cast<uint16_t>(sizeof(s_frame)));
      drew = true;
    }
    break;
  }
  case Mode::Hold:
    break;
  case Mode::Black:
    if (entered || now - lastShow >= 1000) {
      LedBus::clear();
      haveFrame = false;
      drew = true;
    }
    break;
  }

  if (drew || now - lastShow >= 1000) {
    lastShow = now;
    LedBus::show();
  } else {
    yield();
  }
}
