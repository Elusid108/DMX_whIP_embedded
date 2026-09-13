#pragma once

#include <stddef.h>

class NodeId {
public:
  static constexpr size_t kShortMax = 17;
  static constexpr size_t kLongMax = 63;

  static void begin();
  static const char *shortName();
  static const char *longName();
  static bool set(const char *longName, const char *shortName, bool save);
};
