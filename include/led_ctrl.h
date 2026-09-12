#pragma once

#include <stdint.h>

class LedCtrl {
public:
  static void begin();
  static uint8_t get();
  static void set(uint8_t v, bool save);
};
