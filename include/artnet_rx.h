#pragma once

#include <stdint.h>

static constexpr uint16_t kArtNetPort = 6454;
static constexpr uint16_t kArtNetUniverse = 0;

class ArtNetRx {
public:
  static void begin();
  static void stop();
  static void service();
};
