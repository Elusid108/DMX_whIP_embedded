#pragma once

#include <stdint.h>

#include "sd_info.h"

enum class PlaySrc : uint8_t { Root = 0, File = 1, Folder = 2, None = 3 };
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
  // save true also stores this selection as the startup playlist. save false
  // changes the session only (Play / next / prev).
  static bool set(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                  PlayFolderRep folderRep, uint8_t n, bool save,
                  bool reload = true);

  // Startup playlist (NVS). Does not change the session or reload playback.
  static PlaySrc bootSrc();
  static const char *bootPath();
  static PlayFileLoop bootFileLoop();
  static PlayFolderRep bootFolderRep();
  static uint8_t bootFolderN();
  static const char *bootSrcName();
  static const char *bootFileLoopName();
  static const char *bootFolderRepName();
  static bool setStartup(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                         PlayFolderRep folderRep, uint8_t n);
};
