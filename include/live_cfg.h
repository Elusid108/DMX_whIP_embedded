#pragma once

#include <stdint.h>

enum class LiveProto : uint8_t { Auto = 0, ArtNet = 1, Sacn = 2 };

class LiveCfg {
public:
  static void begin();
  static LiveProto proto();
  static uint8_t fps();
  static uint8_t buf();
  static uint32_t showIntervalMs();
  static const char *protoName();
  static bool set(LiveProto proto, uint8_t fps, uint8_t buf, bool save);
};
