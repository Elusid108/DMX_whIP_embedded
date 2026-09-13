#pragma once

#include <stdint.h>

static constexpr uint16_t kSacnPort = 5568;
static constexpr uint16_t kSacnUniverse = 1;

// E1.31 multicast 239.255.(universe >> 8).(universe & 0xFF).
// Universe 1 → 239.255.0.1
static constexpr uint8_t kSacnMcastA = 239;
static constexpr uint8_t kSacnMcastB = 255;

constexpr uint8_t sacnMcastC(uint16_t universe) {
  return static_cast<uint8_t>(universe >> 8);
}

constexpr uint8_t sacnMcastD(uint16_t universe) {
  return static_cast<uint8_t>(universe & 0xFF);
}

class SacnRx {
public:
  static void begin();
  static void stop();
  static void service();
  static void onStaGotIp();
};
