#include "live_input.h"

#include "artnet_rx.h"
#include "fixture.h"
#include "led_bus.h"
#include "log.h"
#include "pixel_map.h"
#include "sacn_rx.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>

namespace {

struct UniSlot {
  LiveSource src;
  uint16_t universe;
  uint8_t dmx[kDmxUniverseSize];
  bool have;
  uint32_t ms;
};

static UniSlot s_slots[kLiveUniSlots];
static uint8_t s_out[kLedCountMax * kMaxChannelsPerPixel];
static uint16_t s_outLen = 0;
static bool s_fresh = false;
static uint8_t s_count = 0;
static uint32_t s_lastMs = 0;
static uint32_t s_lastArtMs = 0;
static uint32_t s_lastSacnMs = 0;
static uint32_t s_drops = 0;
static uint32_t s_statMs = 0;
static uint32_t s_rx = 0;
static uint32_t s_rxMark = 0;
static uint32_t s_ppsMs = 0;
static uint16_t s_pps = 0;
static bool s_psOff = false;

static const char *nameOf(LiveSource src) {
  switch (src) {
  case LiveSource::ArtNet:
    return "artnet";
  case LiveSource::Sacn:
    return "sacn";
  case LiveSource::Mixed:
    return "mixed";
  case LiveSource::None:
  default:
    return "none";
  }
}

static void resetRing() {
  s_fresh = false;
  s_count = 0;
  for (uint8_t i = 0; i < kLiveUniSlots; ++i) {
    s_slots[i].have = false;
    s_slots[i].src = LiveSource::None;
    s_slots[i].universe = 0;
    memset(s_slots[i].dmx, 0, sizeof(s_slots[i].dmx));
  }
}

static bool recent(uint32_t t) {
  return t != 0 && (millis() - t) < kLiveTimeoutMs;
}

static UniSlot *findSlot(LiveSource src, uint16_t universe, bool alloc) {
  int empty = -1;
  for (uint8_t i = 0; i < kLiveUniSlots; ++i) {
    if (s_slots[i].have && s_slots[i].src == src &&
        s_slots[i].universe == universe) {
      return &s_slots[i];
    }
    if (!s_slots[i].have && empty < 0) {
      empty = static_cast<int>(i);
    }
  }
  if (!alloc) {
    return nullptr;
  }
  if (empty < 0) {
    empty = 0;
  }
  UniSlot *s = &s_slots[empty];
  s->src = src;
  s->universe = universe;
  s->have = false;
  memset(s->dmx, 0, sizeof(s->dmx));
  return s;
}

static bool hasSlot(LiveSource src, uint16_t universe) {
  const UniSlot *s = findSlot(src, universe, false);
  return s != nullptr && s->have;
}

// g: global pixel index of the segment's first pixel (advanced here).
static void packSeg(const PixelMapCfg &m, uint8_t seg, uint8_t *dst,
                    uint16_t &o, uint16_t &g, FixDrive drive) {
  const uint8_t ch = m.channelsPerPixel;
  // Consecutive channels almost always share a universe: look the slot up
  // once per universe change, not once per channel.
  LiveSource cSrc = LiveSource::None;
  uint16_t cUni = 0;
  const UniSlot *cSlot = nullptr;
  for (uint16_t p = 0; p < m.pixelCount; ++p, ++g) {
    if (drive == FixDrive::Console) {
      Fixture::consolePixel(g, dst + o, ch);
      o = static_cast<uint16_t>(o + ch);
      continue;
    }
    uint16_t uniOff = 0;
    uint16_t ch1 = 1;
    if (!PixelMap::pixelOrigin(seg, p, uniOff, ch1)) {
      memset(dst + o, 0, ch);
      o = static_cast<uint16_t>(o + ch);
      continue;
    }
    LiveSource src = LiveSource::ArtNet;
    uint16_t uni = static_cast<uint16_t>(m.startArtNetUniverse + uniOff);
    if (m.proto == SegProto::Sacn) {
      src = LiveSource::Sacn;
      uni = static_cast<uint16_t>(m.startSacnUniverse + uniOff);
    } else if (m.proto == SegProto::Auto) {
      const uint16_t au = static_cast<uint16_t>(m.startArtNetUniverse + uniOff);
      const uint16_t su = static_cast<uint16_t>(m.startSacnUniverse + uniOff);
      if (hasSlot(LiveSource::ArtNet, au)) {
        src = LiveSource::ArtNet;
        uni = au;
      } else if (hasSlot(LiveSource::Sacn, su)) {
        src = LiveSource::Sacn;
        uni = su;
      } else {
        src = LiveSource::ArtNet;
        uni = au;
      }
    }
    const uint32_t abs0 = static_cast<uint32_t>(ch1 - 1);
    for (uint8_t k = 0; k < ch; ++k) {
      const uint32_t abs = abs0 + k;
      const uint16_t slotUni =
          static_cast<uint16_t>(uni + abs / kDmxUniverseSize);
      const uint16_t slotOff = static_cast<uint16_t>(abs % kDmxUniverseSize);
      if (cSrc != src || cUni != slotUni) {
        cSrc = src;
        cUni = slotUni;
        cSlot = findSlot(src, slotUni, false);
        if (cSlot != nullptr && !cSlot->have) {
          cSlot = nullptr;
        }
      }
      dst[o++] = cSlot != nullptr ? cSlot->dmx[slotOff] : 0;
    }
    if (drive == FixDrive::Look) {
      Fixture::applyLook(g, dst + o - ch, ch);
    }
  }
}

static uint16_t assembleOutput(uint8_t out, uint8_t *dst, FixDrive drive) {
  uint16_t o = 0;
  uint16_t g = PixelMap::outputPixelOffset(out);
  const uint8_t n = PixelMap::segmentCount();
  for (uint8_t i = 0; i < n; ++i) {
    if (PixelMap::outputOfSegment(i) != out) {
      continue;
    }
    packSeg(PixelMap::segment(i), i, dst, o, g, drive);
  }
  return o;
}

static bool assembleOut0() {
  s_outLen = assembleOutput(0, s_out, FixDrive::None);
  return s_outLen > 0;
}

static void startSockets() {
  if (Fixture::anyArtNet()) {
    ArtNetRx::begin();
  } else {
    ArtNetRx::stop();
  }
  if (Fixture::anySacn()) {
    SacnRx::begin();
    SacnRx::rejoin();
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

} // namespace

void LiveInput::begin() {
  resetRing();
  s_lastMs = 0;
  s_lastArtMs = 0;
  s_lastSacnMs = 0;
  startSockets();
}

void LiveInput::applyCfg() {
  resetRing();
  s_lastMs = 0;
  s_lastArtMs = 0;
  s_lastSacnMs = 0;
  startSockets();
}

void LiveInput::onStaGotIp() {
  if (Fixture::anyArtNet()) {
    ArtNetRx::onStaGotIp();
  }
  if (Fixture::anySacn()) {
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

LiveSource LiveInput::source() {
  const bool a = recent(s_lastArtMs);
  const bool s = recent(s_lastSacnMs);
  if (a && s) {
    return LiveSource::Mixed;
  }
  if (a) {
    return LiveSource::ArtNet;
  }
  if (s) {
    return LiveSource::Sacn;
  }
  return LiveSource::None;
}

const char *LiveInput::sourceName() { return nameOf(source()); }

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
    return false;
  }
  return true;
}

bool LiveInput::push(LiveSource src, const uint8_t *data, uint16_t len,
                     uint16_t universe) {
  if (src == LiveSource::None || src == LiveSource::Mixed || data == nullptr) {
    return false;
  }
  const bool sacn = src == LiveSource::Sacn;
  const bool content = sacn ? PixelMap::wantsSacn(universe) : PixelMap::wantsArtNet(universe);
  const bool fixture = Fixture::isFixtureUniverse(sacn, universe);
  if (!content && !fixture) {
    return false;
  }
  UniSlot *slot = findSlot(src, universe, true);
  if (slot == nullptr) {
    return false;
  }
  if (len > kDmxUniverseSize) {
    len = kDmxUniverseSize;
  }
  ++s_rx;
  const uint32_t now = millis();
  slot->ms = now;
  if (fixture && !content && Fixture::controlOnly()) {
    // Console control for the look (Dim mode): update the levels, but never
    // take over playback as a live stream.
    memcpy(slot->dmx, data, len);
    if (len < kDmxUniverseSize) {
      memset(slot->dmx + len, 0, kDmxUniverseSize - len);
    }
    slot->have = true;
    slot->src = src;
    slot->universe = universe;
    return true;
  }
  if (s_fresh) {
    ++s_drops;
  }
  memcpy(slot->dmx, data, len);
  if (len < kDmxUniverseSize) {
    memset(slot->dmx + len, 0, kDmxUniverseSize - len);
  }
  slot->have = true;
  slot->src = src;
  slot->universe = universe;
  s_fresh = true;
  s_count = 1;
  s_lastMs = now;
  if (src == LiveSource::ArtNet) {
    s_lastArtMs = s_lastMs;
  } else {
    s_lastSacnMs = s_lastMs;
  }
  return true;
}

bool LiveInput::pop(const uint8_t *&dmx, uint16_t &len) {
  if (!s_fresh) {
    return false;
  }
  if (!assembleOut0()) {
    return false;
  }
  s_fresh = false;
  s_count = 0;
  dmx = s_out;
  len = s_outLen;
  return true;
}

bool LiveInput::renderLeds(bool force) {
  if (!s_fresh && !force) {
    return false;
  }
  s_fresh = false;
  s_count = 0;
  const FixDrive drive = Fixture::beginFrame(millis());
  const uint8_t n = PixelMap::outputCount();
  for (uint8_t o = 0; o < n; ++o) {
    const uint16_t len = assembleOutput(o, s_out, drive);
    LedBus::setOutputPacked(o, s_out, len);
  }
  return true;
}

const uint8_t *LiveInput::slotData(LiveSource src, uint16_t universe) {
  const UniSlot *s = findSlot(src, universe, false);
  if (s == nullptr || !s->have || millis() - s->ms >= kLiveTimeoutMs) {
    return nullptr;
  }
  return s->dmx;
}
