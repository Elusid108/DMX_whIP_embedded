#include "live_input.h"

#include "artnet_rx.h"
#include "live_cfg.h"
#include "log.h"
#include "pixel_map.h"
#include "sacn_rx.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>

namespace {

static uint8_t s_uni[kMaxUniverses][kDmxUniverseSize];
static uint8_t s_have = 0;
static uint8_t s_out[kLedCountMax * kMaxChannelsPerPixel];
static uint16_t s_outLen = 0;
static bool s_fresh = false;
static uint8_t s_count = 0;
static uint32_t s_lastMs = 0;
static uint32_t s_drops = 0;
static uint32_t s_statMs = 0;
static uint32_t s_rx = 0;
static uint32_t s_rxMark = 0;
static uint32_t s_ppsMs = 0;
static uint16_t s_pps = 0;
static LiveSource s_lock = LiveSource::None;
static bool s_psOff = false;

static const char *nameOf(LiveSource src) {
  switch (src) {
  case LiveSource::ArtNet:
    return "artnet";
  case LiveSource::Sacn:
    return "sacn";
  case LiveSource::None:
  default:
    return "none";
  }
}

static void resetRing() {
  s_have = 0;
  s_fresh = false;
  s_count = 0;
  memset(s_uni, 0, sizeof(s_uni));
}

static uint16_t startUniverse(LiveSource src) {
  const PixelMapCfg &m = PixelMap::cfg();
  if (src == LiveSource::Sacn) {
    return m.startSacnUniverse;
  }
  return m.startArtNetUniverse;
}

static bool assembleOut() {
  const PixelMapCfg &m = PixelMap::cfg();
  const uint8_t ch = m.channelsPerPixel;
  const uint16_t n = m.pixelCount;
  uint16_t o = 0;
  for (uint16_t p = 0; p < n; ++p) {
    uint16_t uniOff = 0;
    uint16_t ch1 = 1;
    if (!PixelMap::pixelOrigin(p, uniOff, ch1) || uniOff >= kMaxUniverses) {
      memset(s_out + o, 0, ch);
      o = static_cast<uint16_t>(o + ch);
      continue;
    }
    uint32_t abs0 = static_cast<uint32_t>(uniOff) * kDmxUniverseSize +
                    static_cast<uint32_t>(ch1 - 1);
    for (uint8_t k = 0; k < ch; ++k) {
      const uint32_t abs = abs0 + k;
      const uint16_t slotUni = static_cast<uint16_t>(abs / kDmxUniverseSize);
      const uint16_t slotOff = static_cast<uint16_t>(abs % kDmxUniverseSize);
      if (slotUni >= kMaxUniverses ||
          (s_have & static_cast<uint8_t>(1u << slotUni)) == 0) {
        s_out[o++] = 0;
      } else {
        s_out[o++] = s_uni[slotUni][slotOff];
      }
    }
  }
  s_outLen = o;
  return o > 0;
}

static void startSockets() {
  const LiveProto p = LiveCfg::proto();
  if (p == LiveProto::ArtNet || p == LiveProto::Auto) {
    ArtNetRx::begin();
  } else {
    ArtNetRx::stop();
  }
  if (p == LiveProto::Sacn || p == LiveProto::Auto) {
    SacnRx::begin();
  } else {
    SacnRx::stop();
  }
}

static void setPs(bool staUp) {
  if (staUp && !s_psOff) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    s_psOff = true;
    LOG_V("wifi", "ps none (sta)");
  } else if (!staUp && s_psOff) {
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    s_psOff = false;
    LOG_V("wifi", "ps default (ap)");
  }
}

static bool accept(LiveSource src) {
  const LiveProto p = LiveCfg::proto();
  if (p == LiveProto::ArtNet) {
    return src == LiveSource::ArtNet;
  }
  if (p == LiveProto::Sacn) {
    return src == LiveSource::Sacn;
  }
  if (s_lock == LiveSource::None ||
      (s_lastMs != 0 && (millis() - s_lastMs) >= kLiveTimeoutMs)) {
    if (s_lock != src) {
      resetRing();
      LOG_V("live", "auto lock %s", nameOf(src));
    }
    s_lock = src;
    return true;
  }
  return src == s_lock;
}

} // namespace

void LiveInput::begin() {
  resetRing();
  s_lastMs = 0;
  s_lock = LiveSource::None;
  startSockets();
}

void LiveInput::applyCfg() {
  resetRing();
  s_lastMs = 0;
  s_lock = LiveSource::None;
  startSockets();
}

void LiveInput::onStaGotIp() {
  const LiveProto p = LiveCfg::proto();
  if (p == LiveProto::ArtNet || p == LiveProto::Auto) {
    ArtNetRx::onStaGotIp();
  }
  if (p == LiveProto::Sacn || p == LiveProto::Auto) {
    SacnRx::onStaGotIp();
  }
}

void LiveInput::service() {
  ArtNetRx::service();
  SacnRx::service();
  setPs(WiFi.status() == WL_CONNECTED);

  const uint32_t now = millis();
  if (now - s_ppsMs >= 1000) {
    s_ppsMs = now;
    const uint32_t n = s_rx - s_rxMark;
    s_rxMark = s_rx;
    s_pps = n > 65535u ? 65535u : static_cast<uint16_t>(n);
  }
  if (now - s_statMs >= 5000) {
    s_statMs = now;
    if (s_drops > 0) {
      LOG_V("live", "ring drops=%u queued=%u", static_cast<unsigned>(s_drops),
            s_count);
    }
  }
}

LiveSource LiveInput::source() { return s_lock; }

const char *LiveInput::sourceName() { return nameOf(s_lock); }

uint32_t LiveInput::drops() { return s_drops; }

uint8_t LiveInput::queued() { return s_count; }

uint32_t LiveInput::ageMs() {
  if (s_lastMs == 0) {
    return 0;
  }
  return millis() - s_lastMs;
}

uint16_t LiveInput::pps() { return s_pps; }

bool LiveInput::active() {
  if (s_lastMs == 0) {
    return false;
  }
  if ((millis() - s_lastMs) >= kLiveTimeoutMs) {
    s_lock = LiveSource::None;
    return false;
  }
  return true;
}

bool LiveInput::push(LiveSource src, const uint8_t *data, uint16_t len,
                     uint16_t universe) {
  if (src == LiveSource::None || data == nullptr) {
    return false;
  }
  if (!accept(src)) {
    return false;
  }
  const uint16_t start = startUniverse(src);
  const uint16_t span = PixelMap::universeSpan();
  if (universe < start || universe >= static_cast<uint16_t>(start + span)) {
    return false;
  }
  const uint16_t idx = static_cast<uint16_t>(universe - start);
  if (idx >= kMaxUniverses) {
    return false;
  }
  if (len > kDmxUniverseSize) {
    len = kDmxUniverseSize;
  }
  ++s_rx;
  if (s_fresh) {
    ++s_drops;
  }
  memcpy(s_uni[idx], data, len);
  if (len < kDmxUniverseSize) {
    memset(s_uni[idx] + len, 0, kDmxUniverseSize - len);
  }
  s_have = static_cast<uint8_t>(s_have | (1u << idx));
  s_fresh = true;
  s_count = 1;
  s_lastMs = millis();
  return true;
}

bool LiveInput::pop(const uint8_t *&dmx, uint16_t &len) {
  if (!s_fresh || s_have == 0) {
    return false;
  }
  if (!assembleOut()) {
    return false;
  }
  s_fresh = false;
  s_count = 0;
  dmx = s_out;
  len = s_outLen;
  return true;
}
