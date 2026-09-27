#pragma once

#include <stdint.h>

static constexpr uint32_t kLiveTimeoutMs = 2000;

enum class LiveSource : uint8_t { None = 0, ArtNet = 1, Sacn = 2, Mixed = 3 };

class LiveInput {
public:
  static void begin();
  static void service();
  static void applyCfg();
  static void onStaGotIp();
  static bool active();
  static LiveSource source();
  static const char *sourceName();
  static uint32_t drops();
  static uint8_t queued();
  static uint32_t ageMs();
  static uint16_t pps();
  static bool pop(const uint8_t *&dmx, uint16_t &len);
  // force: render even without new data (the fixture animates between
  // frames: strobe, dimmer moves).
  static bool renderLeds(bool force = false);
  // Latest data of a received universe, or nullptr when not heard for
  // kLiveTimeoutMs.
  static const uint8_t *slotData(LiveSource src, uint16_t universe);
  static bool push(LiveSource src, const uint8_t *data, uint16_t len,
                   uint16_t universe);
};
