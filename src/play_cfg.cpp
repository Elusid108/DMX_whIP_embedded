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

struct Sel {
  PlaySrc src;
  char path[kSdPathLen];
  PlayFileLoop fileLoop;
  PlayFolderRep folderRep;
  uint8_t n;
};

static Sel s_sel = {PlaySrc::Root, "/", PlayFileLoop::All, PlayFolderRep::Forever,
                    kNDefault};
static Sel s_boot = {PlaySrc::Root, "/", PlayFileLoop::All,
                     PlayFolderRep::Forever, kNDefault};
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

static bool same(const Sel &a, const Sel &b) {
  return a.src == b.src && a.fileLoop == b.fileLoop && a.folderRep == b.folderRep &&
         a.n == b.n && strcmp(a.path, b.path) == 0;
}

static bool fill(Sel &dst, PlaySrc src, const char *path, PlayFileLoop fileLoop,
                 PlayFolderRep folderRep, uint8_t n) {
  if (n < 1 || n > kNMax) {
    return false;
  }
  const char *usePath = path;
  if (src == PlaySrc::Root) {
    usePath = "/";
  } else if (!validPath(usePath)) {
    return false;
  }
  dst.src = src;
  snprintf(dst.path, sizeof(dst.path), "%s", usePath && usePath[0] ? usePath : "/");
  dst.fileLoop = fileLoop;
  dst.folderRep = folderRep;
  dst.n = n;
  return true;
}

static const char *srcNameOf(PlaySrc src) {
  switch (src) {
  case PlaySrc::File:
    return "file";
  case PlaySrc::Folder:
    return "folder";
  case PlaySrc::Root:
  default:
    return "root";
  }
}

static const char *fileLoopNameOf(PlayFileLoop loop) {
  return loop == PlayFileLoop::One ? "one" : "all";
}

static const char *folderRepNameOf(PlayFolderRep rep) {
  return rep == PlayFolderRep::Count ? "count" : "forever";
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
  PlaySrc src = PlaySrc::Root;
  const uint8_t srcRaw = prefs.getUChar("src", 0);
  if (srcRaw <= static_cast<uint8_t>(PlaySrc::Folder)) {
    src = static_cast<PlaySrc>(srcRaw);
  }
  char path[kSdPathLen] = {};
  const size_t got = prefs.getString("path", path, sizeof(path));
  const char *usePath = (got > 0 && validPath(path)) ? path : "/";
  PlayFileLoop fileLoop = PlayFileLoop::All;
  const uint8_t flp = prefs.getUChar("flp", 1);
  if (flp <= static_cast<uint8_t>(PlayFileLoop::All)) {
    fileLoop = static_cast<PlayFileLoop>(flp);
  }
  PlayFolderRep folderRep = PlayFolderRep::Forever;
  const uint8_t frp = prefs.getUChar("frp", 0);
  if (frp <= static_cast<uint8_t>(PlayFolderRep::Count)) {
    folderRep = static_cast<PlayFolderRep>(frp);
  }
  uint8_t n = kNDefault;
  const uint8_t rawN = prefs.getUChar("n", kNDefault);
  if (rawN >= 1 && rawN <= kNMax) {
    n = rawN;
  }
  prefs.end();

  fill(s_boot, src, usePath, fileLoop, folderRep, n);
  s_sel = s_boot;
}

static void saveBoot() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("play", "nvs open failed");
    return;
  }
  prefs.putUChar("src", static_cast<uint8_t>(s_boot.src));
  prefs.putString("path", s_boot.path);
  prefs.putUChar("flp", static_cast<uint8_t>(s_boot.fileLoop));
  prefs.putUChar("frp", static_cast<uint8_t>(s_boot.folderRep));
  prefs.putUChar("n", s_boot.n);
  prefs.end();
}

static void logSel(const char *tag, const Sel &sel) {
  LOG_V("play", "%s src=%s path=%s file_loop=%s folder_rep=%s n=%u", tag,
        srcNameOf(sel.src), sel.path, fileLoopNameOf(sel.fileLoop),
        folderRepNameOf(sel.folderRep), sel.n);
}

} // namespace

void PlayCfg::begin() {
  loadNvs();
  logSel("cfg", s_sel);
}

PlaySrc PlayCfg::src() {
  loadNvs();
  return s_sel.src;
}

const char *PlayCfg::path() {
  loadNvs();
  return s_sel.path;
}

PlayFileLoop PlayCfg::fileLoop() {
  loadNvs();
  return s_sel.fileLoop;
}

PlayFolderRep PlayCfg::folderRep() {
  loadNvs();
  return s_sel.folderRep;
}

uint8_t PlayCfg::folderN() {
  loadNvs();
  return s_sel.n;
}

const char *PlayCfg::srcName() {
  loadNvs();
  return srcNameOf(s_sel.src);
}

const char *PlayCfg::fileLoopName() {
  loadNvs();
  return fileLoopNameOf(s_sel.fileLoop);
}

const char *PlayCfg::folderRepName() {
  loadNvs();
  return folderRepNameOf(s_sel.folderRep);
}

bool PlayCfg::set(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                  PlayFolderRep folderRep, uint8_t n, bool save, bool reload) {
  loadNvs();
  Sel next = s_sel;
  if (!fill(next, src, path, fileLoop, folderRep, n)) {
    return false;
  }
  const bool changed = !same(s_sel, next);
  s_sel = next;
  if (save) {
    s_boot = s_sel;
    saveBoot();
  }
  if (changed) {
    logSel("cfg", s_sel);
    if (reload) {
      Playback::reload();
    }
  }
  return true;
}

PlaySrc PlayCfg::bootSrc() {
  loadNvs();
  return s_boot.src;
}

const char *PlayCfg::bootPath() {
  loadNvs();
  return s_boot.path;
}

PlayFileLoop PlayCfg::bootFileLoop() {
  loadNvs();
  return s_boot.fileLoop;
}

PlayFolderRep PlayCfg::bootFolderRep() {
  loadNvs();
  return s_boot.folderRep;
}

uint8_t PlayCfg::bootFolderN() {
  loadNvs();
  return s_boot.n;
}

const char *PlayCfg::bootSrcName() {
  loadNvs();
  return srcNameOf(s_boot.src);
}

const char *PlayCfg::bootFileLoopName() {
  loadNvs();
  return fileLoopNameOf(s_boot.fileLoop);
}

const char *PlayCfg::bootFolderRepName() {
  loadNvs();
  return folderRepNameOf(s_boot.folderRep);
}

bool PlayCfg::setStartup(PlaySrc src, const char *path, PlayFileLoop fileLoop,
                         PlayFolderRep folderRep, uint8_t n) {
  loadNvs();
  Sel next = s_boot;
  if (!fill(next, src, path, fileLoop, folderRep, n)) {
    return false;
  }
  const bool changed = !same(s_boot, next);
  s_boot = next;
  saveBoot();
  if (changed) {
    logSel("startup", s_boot);
  }
  return true;
}
