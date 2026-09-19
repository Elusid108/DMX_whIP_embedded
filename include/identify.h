#pragma once

#include <stdint.h>

#include <FastLED.h>

class Identify {
public:
  static void start(uint32_t ms);
  static void cancel();
  static bool active();
  static void render(uint32_t now);
};
