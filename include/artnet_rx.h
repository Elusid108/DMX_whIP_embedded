#pragma once

#include <stdint.h>

static constexpr uint16_t kArtNetPort = 6454;
static constexpr uint16_t kArtNetUniverse = 0;
static constexpr uint32_t kArtNetTimeoutMs = 2000;

class ArtNetRx {
public:
  static void begin();
  static void service();
  static bool hasFresh(uint32_t timeoutMs);
  static const uint8_t *dmx();
  static uint16_t dmxLen();
  static void consume();
};
