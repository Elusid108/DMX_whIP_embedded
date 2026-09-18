#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint32_t kSdPathLen = 64;
static constexpr uint8_t kSdMaxListFiles = 48;
static constexpr uint8_t kSdMaxListDirs = 24;
static constexpr uint8_t kSdMaxPlayFiles = 24;
static constexpr uint8_t kSdMaxDepth = 6;

class SdInfo {
public:
  static void begin();
  static bool remount();
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
  // steal the single handle. Listing still runs (mutex only).
  static void setExclusiveIo(bool on);

  static void refreshTree();
  static uint8_t fileCount();
  static const char *fileAt(uint8_t i);
  static uint8_t dirCount();
  static const char *dirAt(uint8_t i);

  // Fill out[0..*n) with *.dmx paths under dir. Sorted case-insensitive.
  static bool collectPlaylist(const char *dir, bool recursive,
                              char out[][kSdPathLen], uint8_t max, uint8_t *n);
};
