#pragma once

#include <stdint.h>

static constexpr uint16_t kArtNetPort = 6454;
static constexpr uint16_t kArtNetUniverse = 0;
static constexpr uint16_t kArtNetOpPoll = 0x2000;
static constexpr uint16_t kArtNetOpPollReply = 0x2100;
static constexpr uint16_t kArtNetOpDmx = 0x5000;
static constexpr uint16_t kArtNetPollReplyLen = 239;

class ArtNetRx {
public:
  static void begin();
  static void stop();
  static void service();
};
