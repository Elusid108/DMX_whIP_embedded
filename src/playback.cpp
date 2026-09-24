#include "playback.h"

#include "board_profile.h"
#include "dmxrec.h"
#include "log.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "sd_info.h"
#include "sync.h"

#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

static constexpr uint32_t kSdLockForever = 0xFFFFFFFFu;
static constexpr uint32_t kPlayUnderrunLogMs = 5000;
static constexpr uint32_t kCueSeekSlackMs = 200;
static constexpr uint32_t kPlayDiscoverMs = 1000;
static constexpr uint32_t kPlayTaskStack = 8192;
static constexpr UBaseType_t kPlayTaskPrio = 1;
static constexpr uint32_t kPlayPathLen = kSdPathLen;

struct Slot {
  uint32_t t_us;
  uint32_t frame;
  uint16_t size;
  uint8_t rgb[kPlayMaxPayload];
};

enum class ReadResult : uint8_t { Ok = 0, Skip = 1, Eof = 2, Fail = 3 };
enum class SeekKind : uint8_t { None = 0, TimeMs = 1, Frame = 2 };

static SemaphoreHandle_t s_mu = nullptr;
static TaskHandle_t s_task = nullptr;
static File s_file;
static volatile bool s_fileOpen = false;

static Slot s_ring[kPlayRingSlots];
static uint8_t s_head = 0;
static uint8_t s_tail = 0;
static uint8_t s_count = 0;

static char s_path[kPlayPathLen];
static char s_list[kSdMaxPlayFiles][kPlayPathLen];
static uint8_t s_listN = 0;
static uint8_t s_listI = 0;
static uint8_t s_passLeft = 0;
static bool s_hasFile = false;
static volatile bool s_run = false;
static volatile bool s_parked = false;
static volatile bool s_hold = false;
static volatile bool s_userPaused = false;
static bool s_playing = true;
static bool s_loop = true;
static bool s_underrun = false;
static bool s_begun = false;
static bool s_exhausted = false;
static bool s_reload = false;
static bool s_loggedNoFile = false;
static bool s_loggedLoop = false;
static bool s_loggedNoMatch = false;
static uint32_t s_lastUnderrunLog = 0;
static uint32_t s_lastDiscover = 0;
static uint32_t s_off = 0;
static uint32_t s_frameIndex = 0;
static uint32_t s_frameCount = 0;
static uint32_t s_matchedPass = 0;
static uint32_t s_tUs = 0;
static uint32_t s_outFrame = 0;
static uint32_t s_payload = 0;
static uint32_t s_dmxOff = 0;

static SeekKind s_seekKind = SeekKind::None;
static volatile bool s_seekBusy = false;
static uint32_t s_seekMs = 0;
static uint32_t s_seekFrame = 0;
static uint32_t s_landedReqMs = 0xFFFFFFFFu;
static uint32_t s_landedReqFrame = 0xFFFFFFFFu;
static volatile bool s_armCueSeek = false;
static uint32_t s_armCueMs = 0;

static void setHoldFlag(bool on) {
  if (s_hold == on) {
    return;
  }
  s_hold = on;
  LOG_V("play", on ? "hold" : "hold off");
}

static uint32_t s_readTUs = 0;
static uint32_t s_readFrame = 0;
static uint16_t s_readSize = 0;
static uint8_t s_readRgb[kPlayMaxPayload];

static uint32_t sliceBytes() {
  uint32_t n = 0;
  const uint8_t first = PixelMap::firstSegmentOfOutput(0);
  const uint8_t nSeg = PixelMap::segmentCountOfOutput(0);
  for (uint8_t i = 0; i < nSeg; ++i) {
    n += PixelMap::channelCount(static_cast<uint8_t>(first + i));
  }
  if (n == 0) {
    const PixelMapCfg &m = PixelMap::cfg();
    n = static_cast<uint32_t>(m.pixelCount) * m.channelsPerPixel;
  }
  if (n > kPlayMaxPayload) {
    return kPlayMaxPayload;
  }
  return n;
}

static const PixelMapCfg &playMap() {
  return PixelMap::segment(PixelMap::firstSegmentOfOutput(0));
}

static uint32_t dmxStartOff() {
  const uint16_t ch = playMap().startChannel;
  if (ch == 0) {
    return 0;
  }
  return static_cast<uint32_t>(ch - 1);
}

static uint32_t playStartUniverse(uint16_t protocol) {
  const PixelMapCfg &m = playMap();
  if (protocol == kDmxrecProtoArtNet) {
    return m.startArtNetUniverse;
  }
  if (protocol == kDmxrecProtoSacn) {
    return m.startSacnUniverse;
  }
  return 0xFFFFFFFFu;
}

static bool frameForThisNode(uint32_t universe, uint16_t protocol) {
  const uint32_t startUni = playStartUniverse(protocol);
  if (startUni == 0xFFFFFFFFu) {
    return false;
  }
  const uint32_t want = sliceBytes();
  const uint32_t off = dmxStartOff();
  if (want == 0) {
    return false;
  }
  const uint32_t lastUni = startUni + ((off + want - 1) / kDmxrecDmxBytes);
  return universe >= startUni && universe <= lastUni;
}

static bool s_asmActive = false;
static uint32_t s_asmTs = 0;
static uint32_t s_asmIndex = 0;
static uint8_t s_asmRgb[kPlayMaxPayload];
static bool s_pendingHave = false;
static DmxrecFramePrefix s_pendingPrefix;
static uint8_t s_pendingDmx[kDmxrecDmxBytes];
static uint32_t s_pendingIndex = 0;

static void clearAssembler() {
  s_asmActive = false;
  s_pendingHave = false;
  memset(s_asmRgb, 0, sizeof(s_asmRgb));
}

static void copyUniIntoAsm(uint32_t universe, uint16_t protocol,
                           const uint8_t *dmx, uint32_t want) {
  const uint32_t startUni = playStartUniverse(protocol);
  const uint32_t off = dmxStartOff();
  if (startUni == 0xFFFFFFFFu || want == 0) {
    return;
  }
  for (uint32_t i = 0; i < want; ++i) {
    const uint32_t abs = off + i;
    const uint32_t uni = startUni + (abs / kDmxrecDmxBytes);
    const uint32_t slot = abs % kDmxrecDmxBytes;
    if (uni == universe) {
      s_asmRgb[i] = dmx[slot];
    }
  }
}

static char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c + ('a' - 'A'));
  }
  return c;
}

static bool ieq(const char *a, const char *b) {
  while (*a && *b) {
    if (asciiLower(*a++) != asciiLower(*b++)) {
      return false;
    }
  }
  return *a == *b;
}

static bool isRootFilePath(const char *p) {
  if (!p || p[0] != '/') {
    return false;
  }
  return strchr(p + 1, '/') == nullptr;
}

static bool parentDir(const char *path, char *out, size_t n) {
  if (!out || n < 2) {
    return false;
  }
  if (!path || path[0] == '\0') {
    snprintf(out, n, "/");
    return true;
  }
  const char *slash = strrchr(path, '/');
  if (!slash || slash == path) {
    snprintf(out, n, "/");
    return true;
  }
  const size_t len = static_cast<size_t>(slash - path);
  if (len >= n) {
    return false;
  }
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

static bool wrapShow();
static bool bindPath(const char *path);
static bool tryBindPlaylist();

static void lockPlay() {
  if (s_mu) {
    xSemaphoreTake(s_mu, portMAX_DELAY);
  }
}

static void unlockPlay() {
  if (s_mu) {
    xSemaphoreGive(s_mu);
  }
}

static void resetRingLocked() {
  s_head = 0;
  s_tail = 0;
  s_count = 0;
  s_underrun = false;
}

static void noteUnderrunLocked() {
  s_underrun = true;
  const uint32_t now = millis();
  if (s_lastUnderrunLog == 0 || now - s_lastUnderrunLog >= kPlayUnderrunLogMs) {
    LOG_C("play", "underrun");
    s_lastUnderrunLog = now;
  }
}

static void closeFile() {
  if (!s_fileOpen) {
    return;
  }
  if (SdInfo::lock(kSdLockForever)) {
    s_file.close();
    s_fileOpen = false;
    SdInfo::setExclusiveIo(false);
    SdInfo::unlock();
    return;
  }
  s_file.close();
  s_fileOpen = false;
  SdInfo::setExclusiveIo(false);
}

static bool openFileAt(uint32_t off) {
  if (!s_hasFile || s_path[0] == '\0') {
    return false;
  }
  closeFile();
  if (!SdInfo::ok() || !SdInfo::lock(1000)) {
    return false;
  }
  s_file = SD.open(s_path, FILE_READ);
  if (!s_file) {
    SdInfo::unlock();
    LOG_C("play", "open failed %s", s_path);
    return false;
  }
  if (!s_file.seek(off)) {
    s_file.close();
    SdInfo::unlock();
    LOG_C("play", "seek failed %s off=%u", s_path,
          static_cast<unsigned>(off));
    return false;
  }
  s_fileOpen = true;
  SdInfo::setExclusiveIo(true);
  SdInfo::unlock();
  return true;
}

static bool sdRead(void *dst, size_t n) {
  if (!s_fileOpen || n == 0) {
    return n == 0;
  }
  if (!SdInfo::lock(1000)) {
    return false;
  }
  const size_t got = s_file.read(static_cast<uint8_t *>(dst), n);
  const uint32_t pos = s_file.position();
  SdInfo::unlock();
  if (got != n) {
    return false;
  }
  s_off = pos;
  return true;
}

static bool sdSeek(uint32_t off) {
  if (!s_fileOpen) {
    return false;
  }
  if (!SdInfo::lock(1000)) {
    return false;
  }
  const bool ok = s_file.seek(off);
  if (ok) {
    s_off = s_file.position();
  }
  SdInfo::unlock();
  return ok;
}

static bool emitAssembler() {
  if (!s_asmActive) {
    return false;
  }
  lockPlay();
  const uint32_t want = s_payload;
  unlockPlay();
  memset(s_readRgb, 0, sizeof(s_readRgb));
  memcpy(s_readRgb, s_asmRgb, want > kPlayMaxPayload ? kPlayMaxPayload : want);
  s_readSize = static_cast<uint16_t>(want);
  s_readTUs = s_asmTs * 1000u;
  s_readFrame = s_asmIndex;
  s_asmActive = false;
  return true;
}

static ReadResult readOneFrame() {
  for (;;) {
    DmxrecFramePrefix prefix;
    uint8_t dmx[kDmxrecDmxBytes];
    uint32_t index = 0;

    if (s_pendingHave) {
      prefix = s_pendingPrefix;
      memcpy(dmx, s_pendingDmx, sizeof(dmx));
      index = s_pendingIndex;
      s_pendingHave = false;
    } else {
      if (!sdRead(&prefix, sizeof(prefix))) {
        lockPlay();
        const bool eof = s_frameIndex >= s_frameCount;
        unlockPlay();
        if (eof && emitAssembler()) {
          return ReadResult::Ok;
        }
        return eof ? ReadResult::Eof : ReadResult::Fail;
      }
      if (!sdRead(dmx, sizeof(dmx))) {
        return ReadResult::Fail;
      }
      lockPlay();
      index = s_frameIndex;
      const uint32_t frames = s_frameCount;
      s_frameIndex = index + 1;
      unlockPlay();
      if (index + 1 > frames) {
        if (emitAssembler()) {
          return ReadResult::Ok;
        }
        return ReadResult::Eof;
      }
    }

    if (s_asmActive && prefix.t_ms != s_asmTs) {
      s_pendingHave = true;
      s_pendingPrefix = prefix;
      memcpy(s_pendingDmx, dmx, sizeof(dmx));
      s_pendingIndex = index;
      emitAssembler();
      return ReadResult::Ok;
    }

    if (!frameForThisNode(prefix.universe, prefix.protocol)) {
      if (s_asmActive) {
        continue;
      }
      return ReadResult::Skip;
    }

    lockPlay();
    const uint32_t want = s_payload;
    unlockPlay();
    if (!s_asmActive) {
      memset(s_asmRgb, 0, sizeof(s_asmRgb));
      s_asmActive = true;
      s_asmTs = prefix.t_ms;
      s_asmIndex = index;
    }
    copyUniIntoAsm(prefix.universe, prefix.protocol, dmx, want);
  }
}

static bool ringFull() {
  lockPlay();
  const bool full = s_count >= kPlayRingSlots;
  unlockPlay();
  return full;
}

static void pushReadFrame() {
  lockPlay();
  if (s_seekKind != SeekKind::None) {
    unlockPlay();
    return;
  }
  if (s_count < kPlayRingSlots) {
    Slot &s = s_ring[s_head];
    s.t_us = s_readTUs;
    s.frame = s_readFrame;
    s.size = s_readSize;
    memcpy(s.rgb, s_readRgb, s_readSize);
    s_head = static_cast<uint8_t>((s_head + 1) % kPlayRingSlots);
    s_count = static_cast<uint8_t>(s_count + 1);
    s_matchedPass += 1;
  }
  unlockPlay();
}

static bool wrapShow() {
  lockPlay();
  const uint32_t matched = s_matchedPass;
  s_frameIndex = 0;
  s_off = kDmxrecHeaderBytes;
  s_matchedPass = 0;
  const bool first = !s_loggedLoop;
  unlockPlay();
  clearAssembler();

  if (matched == 0) {
    if (!s_loggedNoMatch) {
      LOG_C("play", "no frames for this node %s", s_path);
      s_loggedNoMatch = true;
    }
    return false;
  }
  if (first) {
    s_loggedLoop = true;
    LOG_V("play", "loop %s", s_path);
  }
  return sdSeek(kDmxrecHeaderBytes);
}

static bool readPrefixAt(uint32_t index, DmxrecFramePrefix &prefix) {
  if (!sdSeek(dmxrecFrameOffset(index))) {
    return false;
  }
  return sdRead(&prefix, sizeof(prefix));
}

// First record whose t_ms is at or after want. Records are fixed size, so
// this is a handful of reads instead of a walk from the start of the show.
static bool binaryLandMs(uint32_t wantMs, uint32_t frames, uint32_t &land) {
  uint32_t lo = 0;
  uint32_t hi = frames;
  while (lo < hi) {
    const uint32_t mid = lo + ((hi - lo) / 2);
    DmxrecFramePrefix prefix;
    if (!readPrefixAt(mid, prefix)) {
      return false;
    }
    if (prefix.t_ms < wantMs) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  land = (lo >= frames) ? (frames - 1) : lo;
  return true;
}

static bool nearMs(uint32_t a, uint32_t b) {
  if (a >= b) {
    return (a - b) <= kCueSeekSlackMs;
  }
  return (b - a) <= kCueSeekSlackMs;
}

static bool applySeek() {
  lockPlay();
  const SeekKind kind = s_seekKind;
  const uint32_t wantMs = s_seekMs;
  uint32_t wantFrame = s_seekFrame;
  const uint32_t frames = s_frameCount;
  s_seekKind = SeekKind::None;
  unlockPlay();
  if (kind == SeekKind::None) {
    return true;
  }
  s_seekBusy = true;
  struct BusyGuard {
    ~BusyGuard() { s_seekBusy = false; }
  } busyGuard;
  clearAssembler();

  if (frames == 0) {
    return false;
  }

  if (!s_fileOpen && !openFileAt(kDmxrecHeaderBytes)) {
    lockPlay();
    s_seekKind = kind;
    s_seekMs = wantMs;
    s_seekFrame = wantFrame;
    unlockPlay();
    return false;
  }

  uint32_t land = 0;
  uint32_t want = wantMs;
  if (kind == SeekKind::Frame) {
    land = wantFrame;
    if (land >= frames) {
      land = frames - 1;
    }
  } else {
    if (!sdSeek(kDmxrecHeaderBytes)) {
      lockPlay();
      s_seekKind = kind;
      s_seekMs = wantMs;
      s_seekFrame = wantFrame;
      unlockPlay();
      return false;
    }
    const uint32_t from = s_frameIndex;
    for (uint8_t pass = 0; pass < 3; ++pass) {
      if (!binaryLandMs(want, frames, land)) {
        lockPlay();
        s_seekKind = kind;
        s_seekMs = wantMs;
        s_seekFrame = wantFrame;
        unlockPlay();
        return false;
      }
      if (!Sync::cueFollow() || !Sync::cueHasTime()) {
        break;
      }
      const uint32_t latest = Sync::cueTargetMs();
      if (latest == want) {
        break;
      }
      want = latest;
    }
    if (from + 8 < land || land + 8 < from) {
      LOG_V("play", "catch t_ms=%u frame=%u", static_cast<unsigned>(want),
            static_cast<unsigned>(land));
    }
  }

  const uint32_t off = dmxrecFrameOffset(land);
  if (!sdSeek(off)) {
    lockPlay();
    s_seekKind = kind;
    s_seekMs = wantMs;
    s_seekFrame = wantFrame;
    unlockPlay();
    return false;
  }

  lockPlay();
  s_frameIndex = land;
  s_off = off;
  s_matchedPass = 0;
  if (kind == SeekKind::TimeMs) {
    s_landedReqMs = want;
    s_landedReqFrame = 0xFFFFFFFFu;
  } else {
    s_landedReqFrame = land;
    s_landedReqMs = 0xFFFFFFFFu;
  }
  unlockPlay();
  return true;
}

static void exhaustPlaylist() {
  closeFile();
  lockPlay();
  s_exhausted = true;
  s_hasFile = false;
  s_run = false;
  s_path[0] = '\0';
  s_payload = 0;
  s_frameCount = 0;
  resetRingLocked();
  unlockPlay();
  setHoldFlag(false);
  LOG_V("play", "done (black)");
}

static bool bindIndex(uint8_t i) {
  if (i >= s_listN) {
    return false;
  }
  closeFile();
  lockPlay();
  resetRingLocked();
  s_listI = i;
  unlockPlay();
  return bindPath(s_list[i]);
}

static bool advancePlaylist() {
  lockPlay();
  const bool wrap = s_loop;
  const uint8_t n = s_listN;
  uint8_t i = s_listI;
  const uint8_t left = s_passLeft;
  unlockPlay();

  if (!wrap) {
    vTaskDelay(pdMS_TO_TICKS(20));
    return false;
  }
  if (n == 0) {
    exhaustPlaylist();
    return false;
  }

  if (n == 1) {
    if (left == 1) {
      exhaustPlaylist();
      return false;
    }
    if (left > 1) {
      lockPlay();
      s_passLeft = static_cast<uint8_t>(left - 1);
      unlockPlay();
    }
    if (!wrapShow()) {
      lockPlay();
      s_run = false;
      unlockPlay();
      return false;
    }
    return true;
  }

  i = static_cast<uint8_t>(i + 1);
  if (i >= n) {
    if (left == 1) {
      exhaustPlaylist();
      return false;
    }
    if (left > 1) {
      lockPlay();
      s_passLeft = static_cast<uint8_t>(left - 1);
      unlockPlay();
    }
    i = 0;
    LOG_V("play", "wrap list");
  }

  for (uint8_t k = 0; k < n; ++k) {
    const uint8_t idx = static_cast<uint8_t>((i + k) % n);
    if (bindIndex(idx)) {
      LOG_V("play", "next %s", s_path);
      return true;
    }
  }
  lockPlay();
  s_run = false;
  unlockPlay();
  return false;
}

static void onFileEnd() { advancePlaylist(); }

static bool bindPath(const char *path) {
  DmxrecHeader h;
  uint32_t fileBytes = 0;

  if (!SdInfo::lock(1000)) {
    return false;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    SdInfo::unlock();
    LOG_C("play", "open failed %s", path);
    return false;
  }
  fileBytes = f.size();
  const size_t got = f.read(reinterpret_cast<uint8_t *>(&h), sizeof(h));
  f.close();
  SdInfo::unlock();

  if (got != sizeof(h)) {
    LOG_C("play", "reject %s short", path);
    return false;
  }
  if (!dmxrecMagicOk(h)) {
    LOG_C("play", "reject %s magic", path);
    return false;
  }
  if (h.frame_count == 0) {
    LOG_C("play", "reject %s empty", path);
    return false;
  }
  const uint32_t need = dmxrecFileBytes(h.frame_count);
  if (fileBytes != need) {
    LOG_C("play", "reject %s truncated have=%u need=%u", path,
          static_cast<unsigned>(fileBytes), static_cast<unsigned>(need));
    return false;
  }

  const uint32_t payload = sliceBytes();
  const uint32_t off = dmxStartOff();
  if (payload == 0) {
    LOG_C("play", "reject %s payload", path);
    return false;
  }

  const PixelMapCfg &m = PixelMap::cfg();
  lockPlay();
  snprintf(s_path, sizeof(s_path), "%s", path);
  s_hasFile = true;
  s_payload = payload;
  s_dmxOff = off;
  s_frameCount = h.frame_count;
  s_frameIndex = 0;
  s_off = kDmxrecHeaderBytes;
  s_matchedPass = 0;
  s_loggedLoop = false;
  s_loggedNoMatch = false;
  s_loggedNoFile = false;
  s_seekKind = SeekKind::None;
  s_landedReqMs = 0xFFFFFFFFu;
  s_landedReqFrame = 0xFFFFFFFFu;
  unlockPlay();
  clearAssembler();
  Sync::onPlayFile(s_path);
  if (s_armCueSeek) {
    const uint32_t ms = s_armCueMs;
    s_armCueSeek = false;
    Playback::seekMs(ms);
  }

  LOG_V("play", "file=%s frames=%u payload=%u artnet=%u sacn=%u", s_path,
        static_cast<unsigned>(h.frame_count), static_cast<unsigned>(payload),
        static_cast<unsigned>(m.startArtNetUniverse),
        static_cast<unsigned>(m.startSacnUniverse));
  return true;
}

static bool tryBindPlaylist() {
  if (!SdInfo::ok() || s_exhausted) {
    return false;
  }

  char list[kSdMaxPlayFiles][kPlayPathLen];
  uint8_t n = 0;
  uint8_t startI = 0;
  const PlaySrc src = PlayCfg::src();
  const char *sel = PlayCfg::path();

  if (src == PlaySrc::File && PlayCfg::fileLoop() == PlayFileLoop::One) {
    snprintf(list[0], kPlayPathLen, "%s", sel);
    n = 1;
  } else if (src == PlaySrc::Root ||
             (src == PlaySrc::File && isRootFilePath(sel))) {
    SdInfo::collectPlaylist("/", false, list, kSdMaxPlayFiles, &n);
  } else {
    char dir[kPlayPathLen];
    if (src == PlaySrc::Folder) {
      snprintf(dir, sizeof(dir), "%s", sel);
    } else if (!parentDir(sel, dir, sizeof(dir))) {
      snprintf(dir, sizeof(dir), "/");
    }
    SdInfo::collectPlaylist(dir, true, list, kSdMaxPlayFiles, &n);
  }

  if (n == 0) {
    if (!s_loggedNoFile) {
      LOG_V("play", "no .dmx (idle)");
      s_loggedNoFile = true;
    }
    return false;
  }

  if (src == PlaySrc::File && PlayCfg::fileLoop() == PlayFileLoop::All) {
    for (uint8_t i = 0; i < n; ++i) {
      if (ieq(list[i], sel)) {
        startI = i;
        break;
      }
    }
  }

  uint8_t passes = 0;
  if (src == PlaySrc::Folder &&
      PlayCfg::folderRep() == PlayFolderRep::Count) {
    passes = PlayCfg::folderN();
  }

  lockPlay();
  s_listN = n;
  s_listI = startI;
  s_passLeft = passes;
  for (uint8_t i = 0; i < n; ++i) {
    snprintf(s_list[i], kPlayPathLen, "%s", list[i]);
  }
  unlockPlay();

  LOG_V("play", "list n=%u start=%u src=%s pass=%u", n, startI,
        PlayCfg::srcName(), passes);

  for (uint8_t k = 0; k < n; ++k) {
    const uint8_t i = static_cast<uint8_t>((startI + k) % n);
    if (bindPath(s_list[i])) {
      lockPlay();
      s_listI = i;
      unlockPlay();
      return true;
    }
  }
  return false;
}

static void clearBind() {
  closeFile();
  lockPlay();
  s_hasFile = false;
  s_run = false;
  s_exhausted = false;
  s_path[0] = '\0';
  s_payload = 0;
  s_frameCount = 0;
  s_listN = 0;
  s_listI = 0;
  s_loggedNoMatch = false;
  s_seekKind = SeekKind::None;
  resetRingLocked();
  unlockPlay();
  clearAssembler();
  Sync::onPlayFile("");
}

static void handleReload() {
  closeFile();
  lockPlay();
  s_exhausted = false;
  s_loggedNoFile = false;
  s_loggedNoMatch = false;
  s_hasFile = false;
  s_run = false;
  s_path[0] = '\0';
  s_listN = 0;
  s_listI = 0;
  s_seekKind = SeekKind::None;
  resetRingLocked();
  unlockPlay();
  clearAssembler();
  Sync::onPlayFile("");
  tryBindPlaylist();
}

static void playbackTask(void *) {
  for (;;) {
    lockPlay();
    const bool reload = s_reload;
    if (reload) {
      s_reload = false;
    }
    const bool run = s_run;
    const bool has = s_hasFile;
    const SeekKind seek = s_seekKind;
    unlockPlay();

    if (reload) {
      handleReload();
      continue;
    }

    if (!run || !has) {
      closeFile();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (seek != SeekKind::None) {
      if (!applySeek()) {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      continue;
    }

    if (!s_fileOpen && !openFileAt(s_off)) {
      lockPlay();
      s_run = false;
      unlockPlay();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (ringFull()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    lockPlay();
    const bool atEnd = s_frameIndex >= s_frameCount;
    unlockPlay();
    if (atEnd) {
      onFileEnd();
      continue;
    }

    const ReadResult r = readOneFrame();
    if (r == ReadResult::Ok) {
      pushReadFrame();
      continue;
    }
    if (r == ReadResult::Skip) {
      continue;
    }
    if (r == ReadResult::Eof) {
      onFileEnd();
      continue;
    }

    LOG_C("play", "read failed");
    closeFile();
    lockPlay();
    s_run = false;
    if (!SdInfo::ok()) {
      s_hasFile = false;
    }
    unlockPlay();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

} // namespace

void Playback::begin() {
  if (s_begun) {
    return;
  }
  s_begun = true;
  s_path[0] = '\0';
  if (s_mu == nullptr) {
    s_mu = xSemaphoreCreateMutex();
  }
  LOG_V("play", "ring slots=%u payload=%u (DMXREC; no FastLED)", kPlayRingSlots,
        kPlayMaxPayload);
  tryBindPlaylist();
  if (s_task == nullptr) {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(playbackTask, "play", kPlayTaskStack, nullptr,
                                kPlayTaskPrio, &s_task,
                                BoardProfile::serviceCore());
    if (ok != pdPASS) {
      s_task = nullptr;
      LOG_C("play", "task failed");
    }
  }
}

void Playback::service() {
  if (!s_begun) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastDiscover < kPlayDiscoverMs) {
    return;
  }
  s_lastDiscover = now;

  if (!SdInfo::ok()) {
    if (s_hasFile || s_run || s_exhausted) {
      LOG_V("play", "sd gone");
      clearBind();
    }
    s_loggedNoFile = false;
    return;
  }

  lockPlay();
  const bool reload = s_reload;
  const bool has = s_hasFile;
  const bool exhausted = s_exhausted;
  unlockPlay();
  if (reload) {
    if (s_task == nullptr) {
      lockPlay();
      s_reload = false;
      unlockPlay();
      handleReload();
    }
    return;
  }
  if (!has && !s_fileOpen && !exhausted) {
    tryBindPlaylist();
  }
}

void Playback::start() {
  if (!s_begun) {
    begin();
  }
  lockPlay();
  if (s_parked) {
    unlockPlay();
    return;
  }
  const bool exhausted = s_exhausted;
  bool has = s_hasFile;
  unlockPlay();
  if (exhausted) {
    return;
  }
  if (!has) {
    has = tryBindPlaylist();
  }
  if (!has) {
    return;
  }
  lockPlay();
  if (s_run) {
    unlockPlay();
    return;
  }
  if (s_loggedNoMatch) {
    unlockPlay();
    return;
  }
  s_run = true;
  s_underrun = false;
  s_lastUnderrunLog = 0;
  s_matchedPass = 0;
  unlockPlay();
  LOG_V("play", "start %s", s_path);
}

void Playback::stop() {
  lockPlay();
  const bool was = s_run;
  s_run = false;
  s_playing = false;
  s_userPaused = false;
  unlockPlay();
  if (was) {
    LOG_V("play", "stop");
  }
}

void Playback::park() {
  lockPlay();
  s_run = false;
  s_playing = false;
  s_parked = true;
  s_userPaused = false;
  unlockPlay();
  setHoldFlag(false);
  LOG_V("play", "park");
}

bool Playback::hold() { return s_hold; }

void Playback::setHold(bool on) { setHoldFlag(on); }

void Playback::cueFile(const char *path, uint32_t t_ms, bool savePlaylist) {
  if (!path || !path[0]) {
    return;
  }
  setHoldFlag(true);
  lockPlay();
  const bool same = s_hasFile && !s_reload && strcmp(s_path, path) == 0;
  unlockPlay();
  if (same) {
    seekMs(t_ms);
    play();
    return;
  }
  s_armCueMs = t_ms;
  s_armCueSeek = true;
  PlayCfg::set(PlaySrc::File, path, PlayCfg::fileLoop(), PlayCfg::folderRep(),
               PlayCfg::folderN(), savePlaylist);
  reload();
  play();
}

void Playback::resumeAt(uint32_t t_ms) {
  s_armCueMs = t_ms;
  s_armCueSeek = true;
  reload();
  play();
}

bool Playback::reloadPending() {
  lockPlay();
  const bool on = s_reload;
  unlockPlay();
  return on;
}

void Playback::nudgeCue(uint32_t t_ms) {
  lockPlay();
  const bool arming = s_armCueSeek || s_reload || !s_hasFile;
  if (arming) {
    s_armCueMs = t_ms;
    s_armCueSeek = true;
    unlockPlay();
    return;
  }
  unlockPlay();
  seekMs(t_ms);
}

bool Playback::cueNeedsSeek(uint32_t t_ms) {
  lockPlay();
  const bool arming = s_armCueSeek || s_reload || !s_hasFile;
  if (arming) {
    unlockPlay();
    return true;
  }
  if (s_seekBusy || s_seekKind == SeekKind::TimeMs) {
    unlockPlay();
    return false;
  }
  if (s_count > 0) {
    const uint32_t oldest = s_ring[s_tail].t_us / 1000u;
    const uint8_t newestI =
        static_cast<uint8_t>((s_head + kPlayRingSlots - 1) % kPlayRingSlots);
    const uint32_t newest = s_ring[newestI].t_us / 1000u;
    unlockPlay();
    if (t_ms < oldest) {
      return !nearMs(t_ms, oldest);
    }
    if (t_ms > newest) {
      return !nearMs(t_ms, newest);
    }
    return false;
  }
  const uint32_t landed = s_landedReqMs;
  const uint32_t shown = s_tUs / 1000u;
  const bool haveShown = landed != 0xFFFFFFFFu || s_tUs != 0 || s_matchedPass > 0;
  unlockPlay();
  if (landed != 0xFFFFFFFFu && nearMs(t_ms, landed)) {
    return false;
  }
  if (haveShown && nearMs(t_ms, shown)) {
    return false;
  }
  return true;
}

bool Playback::cueQueued(uint32_t t_ms) {
  lockPlay();
  if (s_count == 0) {
    unlockPlay();
    return false;
  }
  const uint32_t oldest = s_ring[s_tail].t_us / 1000u;
  const uint8_t newestI =
      static_cast<uint8_t>((s_head + kPlayRingSlots - 1) % kPlayRingSlots);
  const uint32_t newest = s_ring[newestI].t_us / 1000u;
  unlockPlay();
  return t_ms >= oldest && t_ms <= newest;
}

void Playback::play() {
  lockPlay();
  s_parked = false;
  s_playing = true;
  s_userPaused = false;
  unlockPlay();
  start();
}

void Playback::pause() {
  lockPlay();
  s_playing = false;
  unlockPlay();
}

void Playback::userPause() {
  lockPlay();
  if (s_parked) {
    unlockPlay();
    return;
  }
  s_userPaused = true;
  s_playing = false;
  unlockPlay();
  LOG_V("play", "pause");
}

void Playback::userResume() {
  lockPlay();
  s_userPaused = false;
  s_parked = false;
  unlockPlay();
  play();
  LOG_V("play", "resume");
}

void Playback::setLoop(bool on) {
  lockPlay();
  s_loop = on;
  unlockPlay();
}

void Playback::reload() {
  if (!s_begun) {
    return;
  }
  lockPlay();
  s_exhausted = false;
  s_loggedNoFile = false;
  s_reload = true;
  s_run = false;
  s_parked = false;
  s_userPaused = false;
  unlockPlay();
}

bool Playback::hasFile() { return s_hasFile; }

bool Playback::parked() { return s_parked; }

bool Playback::userPaused() { return s_userPaused; }

bool Playback::running() { return s_run && s_hasFile; }

bool Playback::playing() { return s_playing && s_hasFile; }

bool Playback::underrun() { return s_underrun; }

uint8_t Playback::available() {
  lockPlay();
  const uint8_t n = s_count;
  unlockPlay();
  return n;
}

bool Playback::peek(uint32_t &t_us) {
  uint32_t frame = 0;
  return peekFrame(t_us, frame);
}

bool Playback::peekFrame(uint32_t &t_us, uint32_t &frame) {
  lockPlay();
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
    }
    unlockPlay();
    return false;
  }
  const Slot &s = s_ring[s_tail];
  t_us = s.t_us;
  frame = s.frame;
  s_tUs = t_us;
  s_outFrame = frame;
  unlockPlay();
  return true;
}

const uint8_t *Playback::peekPayload(size_t &n, uint32_t &t_us) {
  lockPlay();
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
    }
    unlockPlay();
    n = 0;
    t_us = 0;
    return nullptr;
  }
  const Slot &s = s_ring[s_tail];
  n = s.size;
  t_us = s.t_us;
  s_tUs = t_us;
  s_outFrame = s.frame;
  const uint8_t *p = s.rgb;
  unlockPlay();
  return p;
}

bool Playback::copyFrame(uint8_t *rgb, size_t n, uint32_t *t_us) {
  if (!rgb) {
    return false;
  }
  lockPlay();
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
    }
    unlockPlay();
    return false;
  }
  const Slot &s = s_ring[s_tail];
  if (n < s.size) {
    unlockPlay();
    return false;
  }
  memcpy(rgb, s.rgb, s.size);
  s_tUs = s.t_us;
  s_outFrame = s.frame;
  if (t_us) {
    *t_us = s_tUs;
  }
  s_tail = static_cast<uint8_t>((s_tail + 1) % kPlayRingSlots);
  s_count = static_cast<uint8_t>(s_count - 1);
  s_underrun = false;
  unlockPlay();
  return true;
}

bool Playback::catchTick(uint8_t *rgb, size_t n, uint32_t t_ms, uint32_t frame,
                         bool hasTime, bool hasFrame) {
  if (!rgb || (!hasTime && !hasFrame)) {
    return false;
  }
  const uint32_t target_us = t_ms * 1000u;
  lockPlay();
  while (s_count > 0) {
    const Slot &s = s_ring[s_tail];
    bool behind = false;
    if (hasTime) {
      behind = s.t_us < target_us;
    } else if (hasFrame) {
      behind = s.frame < frame;
    }
    if (!behind) {
      break;
    }
    s_tail = static_cast<uint8_t>((s_tail + 1) % kPlayRingSlots);
    s_count = static_cast<uint8_t>(s_count - 1);
  }
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
    }
    unlockPlay();
    return false;
  }
  const Slot &s = s_ring[s_tail];
  if (n < s.size) {
    unlockPlay();
    return false;
  }
  memcpy(rgb, s.rgb, s.size);
  s_tUs = s.t_us;
  s_outFrame = s.frame;
  s_tail = static_cast<uint8_t>((s_tail + 1) % kPlayRingSlots);
  s_count = static_cast<uint8_t>(s_count - 1);
  s_underrun = false;
  unlockPlay();
  return true;
}

bool Playback::seekMs(uint32_t t_ms) {
  lockPlay();
  if (!s_hasFile) {
    s_armCueMs = t_ms;
    s_armCueSeek = true;
    unlockPlay();
    return false;
  }
  if (s_seekKind == SeekKind::TimeMs && s_seekMs == t_ms) {
    unlockPlay();
    return true;
  }
  if (s_seekBusy && nearMs(t_ms, s_seekMs)) {
    unlockPlay();
    return true;
  }
  if (s_count == 0 && s_seekKind == SeekKind::None && s_landedReqMs == t_ms) {
    unlockPlay();
    return true;
  }
  if (s_count > 0) {
    const uint32_t oldest = s_ring[s_tail].t_us / 1000u;
    const uint8_t newestI =
        static_cast<uint8_t>((s_head + kPlayRingSlots - 1) % kPlayRingSlots);
    const uint32_t newest = s_ring[newestI].t_us / 1000u;
    if (t_ms >= oldest && t_ms <= newest) {
      unlockPlay();
      return true;
    }
  }
  s_seekKind = SeekKind::TimeMs;
  s_seekMs = t_ms;
  s_landedReqMs = 0xFFFFFFFFu;
  resetRingLocked();
  unlockPlay();
  return true;
}

bool Playback::seekFrame(uint32_t index) {
  lockPlay();
  if (!s_hasFile) {
    unlockPlay();
    return false;
  }
  if (s_seekKind == SeekKind::Frame && s_seekFrame == index) {
    unlockPlay();
    return true;
  }
  if (s_seekKind == SeekKind::None && s_landedReqFrame == index) {
    unlockPlay();
    return true;
  }
  s_seekKind = SeekKind::Frame;
  s_seekFrame = index;
  s_landedReqFrame = 0xFFFFFFFFu;
  resetRingLocked();
  unlockPlay();
  return true;
}

bool Playback::pop() {
  lockPlay();
  if (s_count == 0) {
    unlockPlay();
    return false;
  }
  s_tail = static_cast<uint8_t>((s_tail + 1) % kPlayRingSlots);
  s_count = static_cast<uint8_t>(s_count - 1);
  unlockPlay();
  return true;
}

uint32_t Playback::tUs() { return s_tUs; }

uint32_t Playback::frameIndex() { return s_outFrame; }

uint16_t Playback::fps() { return 0; }

uint32_t Playback::payloadBytes() {
  lockPlay();
  const uint32_t n = s_hasFile ? s_payload : 0;
  unlockPlay();
  return n;
}

const char *Playback::path() { return s_hasFile ? s_path : ""; }
