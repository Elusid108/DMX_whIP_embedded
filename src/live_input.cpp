#include "live_input.h"

#include "artnet_rx.h"
#include "live_cfg.h"
#include "log.h"
#include "sacn_rx.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>

namespace {

static constexpr uint8_t kSlots = 4;

struct Slot {
  uint8_t dmx[512];
  uint16_t len;
};

static Slot s_ring[kSlots];
static uint8_t s_out[512];
static uint16_t s_outLen = 0;
static uint8_t s_head = 0;
static uint8_t s_tail = 0;
static uint8_t s_count = 0;
static uint32_t s_lastMs = 0;
static uint32_t s_drops = 0;
static uint32_t s_statMs = 0;
static LiveSource s_lock = LiveSource::None;
static bool s_psOff = false;

static const char *sourceName(LiveSource src) {
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
  s_head = 0;
  s_tail = 0;
  s_count = 0;
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
      LOG_V("live", "auto lock %s", sourceName(src));
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
  if (now - s_statMs >= 5000) {
    s_statMs = now;
    if (s_drops > 0) {
      LOG_V("live", "ring drops=%u queued=%u", static_cast<unsigned>(s_drops),
            s_count);
    }
  }
}

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

bool LiveInput::push(LiveSource src, const uint8_t *data, uint16_t len) {
  if (src == LiveSource::None || data == nullptr) {
    return false;
  }
  if (!accept(src)) {
    return false;
  }
  if (len > 512) {
    len = 512;
  }

  const uint8_t depth = LiveCfg::buf();
  if (depth == 0) {
    if (s_count == 1) {
      ++s_drops;
    }
    memcpy(s_ring[0].dmx, data, len);
    if (len < 512) {
      memset(s_ring[0].dmx + len, 0, 512 - len);
    }
    s_ring[0].len = len;
    s_count = 1;
    s_head = 0;
    s_tail = 0;
    s_lastMs = millis();
    return true;
  }

  const uint8_t cap = depth;
  if (s_count == cap) {
    s_head = static_cast<uint8_t>((s_head + 1) % cap);
    --s_count;
    ++s_drops;
  }
  memcpy(s_ring[s_tail].dmx, data, len);
  if (len < 512) {
    memset(s_ring[s_tail].dmx + len, 0, 512 - len);
  }
  s_ring[s_tail].len = len;
  s_tail = static_cast<uint8_t>((s_tail + 1) % cap);
  ++s_count;
  s_lastMs = millis();
  return true;
}

bool LiveInput::pop(const uint8_t *&dmx, uint16_t &len) {
  const uint8_t depth = LiveCfg::buf();
  if (s_count == 0) {
    return false;
  }
  if (depth == 0) {
    memcpy(s_out, s_ring[0].dmx, 512);
    s_outLen = s_ring[0].len;
    s_count = 0;
  } else {
    memcpy(s_out, s_ring[s_head].dmx, 512);
    s_outLen = s_ring[s_head].len;
    s_head = static_cast<uint8_t>((s_head + 1) % depth);
    --s_count;
  }
  dmx = s_out;
  len = s_outLen;
  return true;
}
