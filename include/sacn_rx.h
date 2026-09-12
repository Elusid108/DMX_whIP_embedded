#pragma once

#include <stdint.h>

static constexpr uint16_t kSacnPort = 5568;
static constexpr uint16_t kSacnUniverse = 1;

class SacnRx {
public:
  static void begin();
  static void stop();
  static void service();
  static void onStaGotIp();
};
