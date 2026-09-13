#pragma once

#include <stdint.h>

#include "sd_info.h"

enum class PlaySrc : uint8_t { Root = 0, File = 1, Folder = 2 };
enum class PlayFileLoop : uint8_t { One = 0, All = 1 };
enum class PlayFolderRep : uint8_t { Forever = 0, Count = 1 };

class PlayCfg {
public:
  static void begin();
  static PlaySrc src();
  static const char *path();
  static PlayFileLoop fileLoop();
  static PlayFolderRep folderRep();
  static uint8_t folderN();
  static const char *srcName();
  static const char *fileLoopName();
  static const char *folderRepName();
  static bool set(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                  PlayFolderRep folderRep, uint8_t n, bool save);
};
