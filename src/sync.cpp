#include "sync.h"

#include "log.h"
#include "playback.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

namespace {

static constexpr uint32_t kCueSnapUs = 80000;
static constexpr uint32_t kCueSnapMs = 80;
static constexpr uint32_t kCueJoinRetryMs = 2000;
static constexpr size_t kCueMaxPkt = 32;

static WiFiUDP s_udp;
static uint8_t s_pkt[kCueMaxPkt];
static bool s_begun = false;
static bool s_up = false;
static bool s_mcast = false;
static uint32_t s_joinMs = 0;
static bool s_loggedJoinFail = false;

static uint32_t s_liveSyncMs = 0;
static bool s_liveFence = false;
static bool s_liveActive = false;
static bool s_loggedArtSync = false;
static bool s_loggedE131 = false;

static bool s_follow = false;
static bool s_cuePlay = true;
static bool s_cuePulse = false;
static bool s_hasTime = false;
static bool s_hasFrame = false;
static uint32_t s_targetMs = 0;
static uint32_t s_targetFrame = 0;
static uint32_t s_cueMs = 0;
static bool s_loggedFollow = false;
static uint32_t s_loggedSeekMs = 0xFFFFFFFFu;
static uint32_t s_loggedSeekFrame = 0xFFFFFFFFu;
static uint32_t s_lastSnapMs = 0xFFFFFFFFu;
static uint32_t s_lastSnapFrame = 0xFFFFFFFFu;

static bool magicOk(const uint8_t *p) {
  return p[0] == static_cast<uint8_t>(kCueMagic0) &&
         p[1] == static_cast<uint8_t>(kCueMagic1) &&
         p[2] == static_cast<uint8_t>(kCueMagic2) &&
         p[3] == static_cast<uint8_t>(kCueMagic3);
}

static IPAddress cueGroup() {
  return IPAddress(kCueMcastA, kCueMcastB, kCueMcastC, kCueMcastD);
}

static bool bindUnicast() {
  if (!s_udp.begin(kCuePort)) {
    LOG_C("sync", "udp bind :%u failed", kCuePort);
    s_up = false;
    s_mcast = false;
    return false;
  }
  s_up = true;
  s_mcast = false;
  return true;
}

static bool joinMcast() {
  if (!s_up || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const uint32_t now = millis();
  if (s_joinMs != 0 && now - s_joinMs < kCueJoinRetryMs) {
    return false;
  }
  s_joinMs = now;
  const IPAddress group = cueGroup();
  s_udp.stop();
  if (!s_udp.beginMulticast(group, kCuePort)) {
    if (!s_loggedJoinFail) {
      s_loggedJoinFail = true;
      LOG_C("sync", "mcast join failed");
    }
    if (!bindUnicast()) {
      return false;
    }
    return false;
  }
  s_up = true;
  s_mcast = true;
  s_loggedJoinFail = false;
  LOG_V("sync", "cue %s:%u", group.toString().c_str(), kCuePort);
  return true;
}

static void enterFollow() {
  if (s_follow) {
    return;
  }
  s_follow = true;
  Playback::setLoop(false);
  if (!s_loggedFollow) {
    s_loggedFollow = true;
    LOG_V("sync", "cue follow (bus)");
  }
}

static void leaveFollow() {
  if (!s_follow) {
    return;
  }
  s_follow = false;
  s_cuePlay = true;
  s_hasTime = false;
  s_hasFrame = false;
  s_cuePulse = false;
  s_lastSnapMs = 0xFFFFFFFFu;
  s_lastSnapFrame = 0xFFFFFFFFu;
  Playback::setLoop(true);
  Playback::play();
  LOG_V("sync", "cue silent → local auto-play");
  s_loggedFollow = false;
}

static void applyPosition(uint8_t flags, uint32_t t_ms, uint32_t frame,
                          bool forceSeek) {
  s_hasTime = (flags & kCueFlagHasTime) != 0;
  s_hasFrame = (flags & kCueFlagHasFrame) != 0;
  if (s_hasTime) {
    s_targetMs = t_ms;
  }
  if (s_hasFrame) {
    s_targetFrame = frame;
  }
  if (!s_hasTime && !s_hasFrame) {
    return;
  }

  bool seek = forceSeek;
  const uint8_t queued = Playback::available();
  if (!seek && queued > 0) {
    uint32_t t_us = 0;
    uint32_t curFrame = 0;
    if (Playback::peekFrame(t_us, curFrame)) {
      if (s_hasTime) {
        const uint32_t target_us = s_targetMs * 1000u;
        if (t_us + kCueSnapUs < target_us || target_us + kCueSnapUs < t_us) {
          seek = true;
        }
      } else if (s_hasFrame) {
        const uint32_t a = curFrame;
        const uint32_t b = s_targetFrame;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > 4) {
          seek = true;
        }
      }
    }
  } else if (!seek && queued == 0) {
    if (s_hasTime) {
      if (s_lastSnapMs == 0xFFFFFFFFu) {
        seek = true;
      } else {
        const uint32_t a = s_targetMs;
        const uint32_t b = s_lastSnapMs;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > kCueSnapMs) {
          seek = true;
        }
      }
    } else if (s_hasFrame) {
      if (s_lastSnapFrame == 0xFFFFFFFFu) {
        seek = true;
      } else {
        const uint32_t a = s_targetFrame;
        const uint32_t b = s_lastSnapFrame;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > 4) {
          seek = true;
        }
      }
    }
  }

  if (!seek) {
    return;
  }
  if (s_hasFrame && !s_hasTime) {
    Playback::seekFrame(s_targetFrame);
    s_lastSnapFrame = s_targetFrame;
  } else if (s_hasTime) {
    Playback::seekMs(s_targetMs);
    s_lastSnapMs = s_targetMs;
  }
}

static const char *opName(uint8_t op) {
  switch (op) {
  case kCueOpPlay:
    return "play";
  case kCueOpPause:
    return "pause";
  case kCueOpSeek:
    return "seek";
  case kCueOpTick:
    return "tick";
  default:
    return "op";
  }
}

static void onCue(const CuePacket &p) {
  s_cueMs = millis();
  const bool first = !s_follow;
  enterFollow();

  const bool hasPos =
      (p.flags & (kCueFlagHasTime | kCueFlagHasFrame)) != 0;

  switch (p.opcode) {
  case kCueOpPlay: {
    const bool rising = first || !s_cuePlay;
    s_cuePlay = true;
    applyPosition(p.flags, p.t_ms, p.frame, rising);
    Playback::play();
    if (hasPos) {
      s_cuePulse = true;
    }
    if (rising) {
      LOG_V("sync", "cue play t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  }
  case kCueOpPause: {
    const bool falling = first || s_cuePlay;
    s_cuePlay = false;
    applyPosition(p.flags, p.t_ms, p.frame, hasPos);
    Playback::pause();
    if (falling) {
      LOG_V("sync", "cue pause t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  }
  case kCueOpSeek:
    applyPosition(p.flags, p.t_ms, p.frame, true);
    if (s_cuePlay && hasPos) {
      s_cuePulse = true;
    }
    if (p.t_ms != s_loggedSeekMs || p.frame != s_loggedSeekFrame) {
      s_loggedSeekMs = p.t_ms;
      s_loggedSeekFrame = p.frame;
      LOG_V("sync", "cue seek t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  case kCueOpTick:
    applyPosition(p.flags, p.t_ms, p.frame, first);
    if (s_cuePlay && hasPos) {
      s_cuePulse = true;
    }
    break;
  default:
    LOG_V("sync", "cue %s ignored", opName(p.opcode));
    break;
  }
}

static void parseCue(int n, const IPAddress &from) {
  (void)from;
  if (n < static_cast<int>(kCuePktLen)) {
    return;
  }
  if (!magicOk(s_pkt) || s_pkt[4] != kCueVersion) {
    return;
  }
  CuePacket p;
  memcpy(&p, s_pkt, sizeof(p));
  onCue(p);
}

} // namespace

void Sync::begin() {
  if (s_begun) {
    return;
  }
  s_begun = true;
  if (!bindUnicast()) {
    return;
  }
  LOG_V("sync", "listen :%u (auto-play until cue)", kCuePort);
  joinMcast();
}

void Sync::service() {
  if (!s_begun) {
    return;
  }
  if (s_up && !s_mcast) {
    joinMcast();
  }
  if (!s_up && s_begun) {
    bindUnicast();
  }

  if (s_up) {
    for (;;) {
      const int n = s_udp.parsePacket();
      if (n <= 0) {
        break;
      }
      const IPAddress from = s_udp.remoteIP();
      const int got =
          s_udp.read(s_pkt, n > static_cast<int>(kCueMaxPkt) ? kCueMaxPkt : n);
      if (got > 0) {
        parseCue(got, from);
      }
    }
  }

  const uint32_t now = millis();
  if (s_liveActive &&
      (s_liveSyncMs == 0 || now - s_liveSyncMs >= kLiveSyncHoldMs)) {
    s_liveActive = false;
    s_liveFence = false;
    LOG_V("sync", "live free-run (no ArtSync/E1.31)");
  }
  if (s_follow && (s_cueMs == 0 || now - s_cueMs >= kCueHoldMs)) {
    leaveFollow();
  }
}

void Sync::onArtSync() {
  s_liveSyncMs = millis();
  s_liveFence = true;
  if (!s_liveActive) {
    s_liveActive = true;
    LOG_V("sync", "live fence ArtSync");
  }
  if (!s_loggedArtSync) {
    s_loggedArtSync = true;
    LOG_V("sync", "ArtSync");
  }
}

void Sync::onE131Sync(uint16_t universe) {
  s_liveSyncMs = millis();
  s_liveFence = true;
  if (!s_liveActive) {
    s_liveActive = true;
    LOG_V("sync", "live fence E1.31 uni=%u", universe);
  }
  if (!s_loggedE131) {
    s_loggedE131 = true;
    LOG_V("sync", "E1.31 sync uni=%u", universe);
  }
}

bool Sync::liveSyncActive() { return s_liveActive; }

bool Sync::hasLiveFence() { return s_liveActive && s_liveFence; }

bool Sync::takeLiveFence() {
  if (!s_liveFence) {
    return false;
  }
  s_liveFence = false;
  return true;
}

bool Sync::cueFollow() { return s_follow; }

bool Sync::cuePlaying() { return s_cuePlay; }

bool Sync::cueSocketUp() { return s_up; }

bool Sync::hasCuePulse() { return s_follow && s_cuePlay && s_cuePulse; }

bool Sync::takeCuePulse() {
  if (!s_cuePulse) {
    return false;
  }
  s_cuePulse = false;
  return true;
}

bool Sync::cueHasTime() { return s_hasTime; }

bool Sync::cueHasFrame() { return s_hasFrame; }

uint32_t Sync::cueTargetMs() { return s_targetMs; }

uint32_t Sync::cueTargetFrame() { return s_targetFrame; }
