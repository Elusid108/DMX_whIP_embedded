#pragma once

#include <stdint.h>

class SdInfo {
public:
  static void begin();
  static void service();
  static bool ok();
  static const char *type();
  static uint32_t sizeMb();
  static uint32_t usedMb();
  static uint32_t freeMb();
};
