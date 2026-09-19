#pragma once

#include <stdint.h>

#include <FastLED.h>

class LedBus {
public:
  static void begin();
  static void apply();
  static void requestApply();
  static void service();
  static void show();
  static CRGB *leds();
  static uint16_t count();
  static void setRgb(uint16_t i, uint8_t r, uint8_t g, uint8_t b);
  static void setPacked(uint16_t i, const uint8_t *ch);
  static void fillRgb(uint8_t r, uint8_t g, uint8_t b);
  static void clear();
};
