#include "play_cfg.h"

#include "log.h"
#include "playback.h"

#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr char kPrefsNs[] = "play";
static constexpr uint8_t kNDefault = 1;
static constexpr uint8_t kNMax = 99;

static PlaySrc s_src = PlaySrc::Root;
static char s_path[kSdPathLen] = "/";
static PlayFileLoop s_fileLoop = PlayFileLoop::All;
static PlayFolderRep s_folderRep = PlayFolderRep::Forever;
static uint8_t s_n = kNDefault;
static bool s_loaded = false;

static bool validPath(const char *p) {
  if (!p || p[0] != '/') {
    return false;
  }
  const size_t n = strlen(p);
  if (n == 0 || n >= kSdPathLen) {
    return false;
  }
  if (strstr(p, "..") != nullptr) {
    return false;
  }
  return true;
}

static void copyPath(const char *p) {
  snprintf(s_path, sizeof(s_path), "%s", p && p[0] ? p : "/");
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  const uint8_t src = prefs.getUChar("src", 0);
  if (src <= static_cast<uint8_t>(PlaySrc::Folder)) {
    s_src = static_cast<PlaySrc>(src);
  }
  char path[kSdPathLen] = {};
  const size_t got = prefs.getString("path", path, sizeof(path));
  if (got > 0 && validPath(path)) {
    copyPath(path);
  }
  const uint8_t flp = prefs.getUChar("flp", 1);
  if (flp <= static_cast<uint8_t>(PlayFileLoop::All)) {
    s_fileLoop = static_cast<PlayFileLoop>(flp);
  }
  const uint8_t frp = prefs.getUChar("frp", 0);
  if (frp <= static_cast<uint8_t>(PlayFolderRep::Count)) {
    s_folderRep = static_cast<PlayFolderRep>(frp);
  }
  const uint8_t n = prefs.getUChar("n", kNDefault);
  if (n >= 1 && n <= kNMax) {
    s_n = n;
  }
  prefs.end();

  if (s_src == PlaySrc::Root) {
    copyPath("/");
  }
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("play", "nvs open failed");
    return;
  }
  prefs.putUChar("src", static_cast<uint8_t>(s_src));
  prefs.putString("path", s_path);
  prefs.putUChar("flp", static_cast<uint8_t>(s_fileLoop));
  prefs.putUChar("frp", static_cast<uint8_t>(s_folderRep));
  prefs.putUChar("n", s_n);
  prefs.end();
}

} // namespace

void PlayCfg::begin() {
  loadNvs();
  LOG_V("play", "cfg src=%s path=%s file_loop=%s folder_rep=%s n=%u", srcName(),
        s_path, fileLoopName(), folderRepName(), s_n);
}

PlaySrc PlayCfg::src() {
  loadNvs();
  return s_src;
}

const char *PlayCfg::path() {
  loadNvs();
  return s_path;
}

PlayFileLoop PlayCfg::fileLoop() {
  loadNvs();
  return s_fileLoop;
}

PlayFolderRep PlayCfg::folderRep() {
  loadNvs();
  return s_folderRep;
}

uint8_t PlayCfg::folderN() {
  loadNvs();
  return s_n;
}

const char *PlayCfg::srcName() {
  loadNvs();
  switch (s_src) {
  case PlaySrc::File:
    return "file";
  case PlaySrc::Folder:
    return "folder";
  case PlaySrc::Root:
  default:
    return "root";
  }
}

const char *PlayCfg::fileLoopName() {
  loadNvs();
  return s_fileLoop == PlayFileLoop::One ? "one" : "all";
}

const char *PlayCfg::folderRepName() {
  loadNvs();
  return s_folderRep == PlayFolderRep::Count ? "count" : "forever";
}

bool PlayCfg::set(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                  PlayFolderRep folderRep, uint8_t n, bool save) {
  if (n < 1 || n > kNMax) {
    return false;
  }
  const char *usePath = path;
  if (src == PlaySrc::Root) {
    usePath = "/";
  } else if (!validPath(usePath)) {
    return false;
  }

  loadNvs();
  const bool changed = src != s_src || fileLoop != s_fileLoop ||
                       folderRep != s_folderRep || n != s_n ||
                       strcmp(s_path, usePath) != 0;
  s_src = src;
  copyPath(usePath);
  s_fileLoop = fileLoop;
  s_folderRep = folderRep;
  s_n = n;
  if (save) {
    saveNvs();
  }
  if (changed) {
    LOG_V("play", "cfg src=%s path=%s file_loop=%s folder_rep=%s n=%u",
          srcName(), s_path, fileLoopName(), folderRepName(), s_n);
    Playback::reload();
  }
  return true;
}
