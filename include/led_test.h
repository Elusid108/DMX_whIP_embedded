#pragma once

#include <stdint.h>

enum class LedTestMode : uint8_t {
  Off = 0,
  Rainbow,
  Cycle,
  Ends,
};

// RAM-only patterns for one output at a time. Live input cancels them.
class LedTest {
public:
  static bool set(uint8_t out, LedTestMode mode);
  static void cancel();
  static bool active();
  static LedTestMode mode(uint8_t out);
  static const char *modeName(uint8_t out);
  static void render(uint32_t now);
};
