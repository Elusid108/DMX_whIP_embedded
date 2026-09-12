#pragma once

#include "artnet_rx.h"

class LiveInput {
public:
  static bool active() { return ArtNetRx::hasFresh(kArtNetTimeoutMs); }
};
