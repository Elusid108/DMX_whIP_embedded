#pragma once

#include <stddef.h>
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

  // Serialize SPI SD across the portal hotplug path and the playback reader.
  // waitMs == 0xFFFFFFFF waits forever.
  static bool lock(uint32_t waitMs);
  static void unlock();

  // Playback has an open File. Skip mount/root probes so SPI SD does not
  // steal the single handle.
  static void setExclusiveIo(bool on);

  // Prefers "/show.dwr", else the first *.dwr in "/". False if unmounted/none.
  static bool findDwr(char *path, size_t pathLen);
};
