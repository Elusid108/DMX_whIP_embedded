#include "playback.h"

#include "board_profile.h"
#include "dmxrec.h"
#include "log.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "sd_info.h"
#include "sync.h"
#include "sync_net.h"

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
  uint16_t pass;
  uint16_t size;
  uint8_t rgb[kPlayMaxPayload];
};

enum class ReadResult : uint8_t { Ok = 0, Skip = 1, Eof = 2, Fail = 3 };
enum class SeekKind : uint8_t { None = 0, TimeMs = 1, Frame = 2 };

static SemaphoreHandle_t s_mu = nullptr;
static TaskHandle_t s_task = nullptr;
static File s_file;
static volatile bool s_fileOpen = false;

static Slot *s_ring = nullptr;
static uint8_t s_slots = 0;
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

// Local show clock: showPos = now + s_clkOff while armed. It anchors on the
// first frame after start/seek/underrun (pause on underrun, never invent).
static constexpr uint32_t kRewindSlackMs = 200;
static bool s_clkArmed = false;
static int32_t s_clkOff = 0;
static uint32_t s_lastShownMs = 0;
static uint32_t s_lastGapMs = 0;

// Group schedule (cue bus): position = startPos + (masterNow - startAt), on
// the shared network clock. Loops wrap every dur ms; each ring frame carries
// the reader pass it was read in, so a wrap never strands queued frames.
struct Schedule {
  bool on;
  bool paused;
  bool loop;
  int64_t startAtUs;
  uint32_t startPosMs;
  uint32_t durMs;
  uint32_t pausePosMs;
};
static Schedule s_sched = {};
static uint16_t s_readPass = 0;
static uint16_t s_seekPass = 0;
static uint32_t s_durMs = 0;

// Sequential SD reads go through one block buffer (one lock per block
// instead of two per 522-byte record).
static constexpr uint32_t kReadBufPsram = 16384;
static constexpr uint32_t kReadBufMin = 4096;
static uint8_t *s_rbuf = nullptr;
static uint32_t s_rbufCap = 0;
static uint32_t s_rbufPos = 0;
static uint32_t s_rbufLen = 0;

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

// Records are one universe each, stamped with their own arrival ms. A frame
// is every record within kAsmWindowMs of the first, until a universe repeats.
// s_asmRgb keeps the last value of every universe across frames, so a
// universe that lands a millisecond late never blanks the others.
static constexpr uint32_t kAsmWindowMs = 4;
static bool s_asmActive = false;
static uint32_t s_asmTs = 0;
static uint32_t s_asmIndex = 0;
static uint32_t s_asmSeen = 0;
static uint8_t s_asmRgb[kPlayMaxPayload];
static bool s_pendingHave = false;
static DmxrecFramePrefix s_pendingPrefix;
static uint8_t s_pendingDmx[kDmxrecDmxBytes];
static uint32_t s_pendingIndex = 0;

static void clearAssembler() {
  s_asmActive = false;
  s_asmSeen = 0;
  s_pendingHave = false;
  memset(s_asmRgb, 0, sizeof(s_asmRgb));
}

static uint32_t asmBit(uint32_t universe, uint16_t protocol) {
  const uint32_t startUni = playStartUniverse(protocol);
  const uint32_t i = universe - startUni +
                     (protocol == kDmxrecProtoSacn ? 16u : 0u);
  return i < 32 ? (1u << i) : 0;
}

static void copyUniIntoAsm(uint32_t universe, uint16_t protocol,
                           const uint8_t *dmx, uint32_t want) {
  const uint32_t startUni = playStartUniverse(protocol);
  const uint32_t off = dmxStartOff();
  if (startUni == 0xFFFFFFFFu || want == 0 || universe < startUni) {
    return;
  }
  // This universe covers absolute bytes [base, base + 512) of the window.
  const uint32_t base = (universe - startUni) * kDmxrecDmxBytes;
  const uint32_t from = base > off ? base - off : 0;
  const uint32_t endAbs = base + kDmxrecDmxBytes;
  if (endAbs <= off) {
    return;
  }
  uint32_t to = endAbs - off;
  if (to > want) {
    to = want;
  }
  if (from >= to) {
    return;
  }
  memcpy(s_asmRgb + from, dmx + (off + from - base), to - from);
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

static void clearClockLocked() { s_clkArmed = false; }

static void resetRingLocked() {
  s_head = 0;
  s_tail = 0;
  s_count = 0;
  s_underrun = false;
  clearClockLocked();
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
  s_rbufLen = 0;
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
  s_off = off;
  s_rbufLen = 0;
  SdInfo::setExclusiveIo(true);
  SdInfo::unlock();
  return true;
}

// Refill the block buffer at s_off. Caller checked s_fileOpen.
static bool refillReadBuf() {
  if (!SdInfo::lock(1000)) {
    return false;
  }
  bool ok = s_file.position() == s_off || s_file.seek(s_off);
  int got = 0;
  if (ok) {
    got = s_file.read(s_rbuf, s_rbufCap);
  }
  SdInfo::unlock();
  if (!ok || got <= 0) {
    s_rbufLen = 0;
    return false;
  }
  s_rbufPos = s_off;
  s_rbufLen = static_cast<uint32_t>(got);
  return true;
}

// Sequential read at s_off through the block buffer.
static bool sdRead(void *dst, size_t n) {
  if (!s_fileOpen || n == 0) {
    return n == 0;
  }
  uint8_t *out = static_cast<uint8_t *>(dst);
  while (n > 0) {
    if (s_off < s_rbufPos || s_off >= s_rbufPos + s_rbufLen) {
      if (!refillReadBuf()) {
        return false;
      }
    }
    const uint32_t at = s_off - s_rbufPos;
    uint32_t take = s_rbufLen - at;
    if (take > n) {
      take = static_cast<uint32_t>(n);
    }
    memcpy(out, s_rbuf + at, take);
    out += take;
    n -= take;
    s_off += take;
  }
  return true;
}

// Position the next sequential read. The buffer stays valid if it covers off.
static bool sdSeek(uint32_t off) {
  if (!s_fileOpen) {
    return false;
  }
  s_off = off;
  return true;
}

// Small random read (binary seek probes) without refilling the block buffer.
static bool sdReadAt(uint32_t off, void *dst, size_t n) {
  if (!s_fileOpen) {
    return false;
  }
  if (off >= s_rbufPos && off + n <= s_rbufPos + s_rbufLen) {
    memcpy(dst, s_rbuf + (off - s_rbufPos), n);
    return true;
  }
  if (!SdInfo::lock(1000)) {
    return false;
  }
  const bool ok = s_file.seek(off) &&
                  s_file.read(static_cast<uint8_t *>(dst), n) ==
                      static_cast<int>(n);
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
  s_asmSeen = 0;
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

    const bool mine = frameForThisNode(prefix.universe, prefix.protocol);
    const uint32_t bit = mine ? asmBit(prefix.universe, prefix.protocol) : 0;
    if (s_asmActive &&
        (prefix.t_ms < s_asmTs || prefix.t_ms - s_asmTs >= kAsmWindowMs ||
         (s_asmSeen & bit) != 0)) {
      s_pendingHave = true;
      s_pendingPrefix = prefix;
      memcpy(s_pendingDmx, dmx, sizeof(dmx));
      s_pendingIndex = index;
      emitAssembler();
      return ReadResult::Ok;
    }

    if (!mine) {
      if (s_asmActive) {
        continue;
      }
      return ReadResult::Skip;
    }

    lockPlay();
    const uint32_t want = s_payload;
    unlockPlay();
    if (!s_asmActive) {
      s_asmActive = true;
      s_asmTs = prefix.t_ms;
      s_asmIndex = index;
    }
    s_asmSeen |= bit;
    copyUniIntoAsm(prefix.universe, prefix.protocol, dmx, want);
  }
}

static bool ringFull() {
  lockPlay();
  const bool full = s_count >= s_slots;
  unlockPlay();
  return full;
}

static void pushReadFrame() {
  lockPlay();
  if (s_seekKind != SeekKind::None) {
    unlockPlay();
    return;
  }
  if (s_count < s_slots) {
    Slot &s = s_ring[s_head];
    s.t_us = s_readTUs;
    s.frame = s_readFrame;
    s.pass = s_readPass;
    s.size = s_readSize;
    memcpy(s.rgb, s_readRgb, s_readSize);
    s_head = static_cast<uint8_t>((s_head + 1) % s_slots);
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
  s_readPass = static_cast<uint16_t>(s_readPass + 1);
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
  return sdReadAt(dmxrecFrameOffset(index), &prefix, sizeof(prefix));
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
    if (!binaryLandMs(want, frames, land)) {
      lockPlay();
      s_seekKind = kind;
      s_seekMs = wantMs;
      s_seekFrame = wantFrame;
      unlockPlay();
      return false;
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
  s_readPass = s_seekPass;
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
      Sync::notePlaylistAdvance();
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
  // Show length: last record time plus one frame gap (first two distinct
  // record times), so a loop holds the last frame for a frame.
  uint32_t durMs = 0;
  if (got == sizeof(h) && h.frame_count > 0 &&
      fileBytes == dmxrecFileBytes(h.frame_count)) {
    DmxrecFramePrefix pre;
    uint32_t first = 0;
    uint32_t gap = 0;
    const uint32_t probe = h.frame_count < 64 ? h.frame_count : 64;
    for (uint32_t i = 0; i < probe && gap == 0; ++i) {
      if (!f.seek(dmxrecFrameOffset(i)) ||
          f.read(reinterpret_cast<uint8_t *>(&pre), sizeof(pre)) != sizeof(pre)) {
        break;
      }
      if (i == 0) {
        first = pre.t_ms;
      } else if (pre.t_ms > first) {
        gap = pre.t_ms - first;
      }
    }
    if (f.seek(dmxrecFrameOffset(h.frame_count - 1)) &&
        f.read(reinterpret_cast<uint8_t *>(&pre), sizeof(pre)) == sizeof(pre)) {
      durMs = pre.t_ms + (gap ? gap : 25);
    }
  }
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
  s_durMs = durMs;
  s_frameIndex = 0;
  s_off = kDmxrecHeaderBytes;
  s_matchedPass = 0;
  s_readPass = 0;
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
  if (PlayCfg::src() == PlaySrc::None) {
    return false;
  }
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
    // Binding the next file resets the ring: let the queued tail of this one
    // play out first (a deep ring would otherwise cut up to a second).
    const bool drain = s_loop && s_listN > 1 && s_count > 0;
    unlockPlay();
    if (atEnd) {
      if (drain) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
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
      lockPlay();
      const bool drainEof = s_loop && s_listN > 1 && s_count > 0;
      unlockPlay();
      if (drainEof) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
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
  if (s_ring == nullptr) {
    const bool psram = psramFound();
    s_slots = psram ? kPlayRingSlotsPsram : kPlayRingSlotsMin;
    s_rbufCap = psram ? kReadBufPsram : kReadBufMin;
    const size_t ringBytes = sizeof(Slot) * s_slots;
    if (psram) {
      s_ring = static_cast<Slot *>(ps_malloc(ringBytes));
      s_rbuf = static_cast<uint8_t *>(ps_malloc(s_rbufCap));
    }
    if (s_ring == nullptr) {
      s_slots = kPlayRingSlotsMin;
      s_ring = static_cast<Slot *>(malloc(sizeof(Slot) * s_slots));
    }
    if (s_rbuf == nullptr) {
      s_rbufCap = kReadBufMin;
      s_rbuf = static_cast<uint8_t *>(malloc(s_rbufCap));
    }
  }
  LOG_V("play", "ring slots=%u payload=%u rbuf=%u (DMXREC; no FastLED)",
        s_slots, kPlayMaxPayload, static_cast<unsigned>(s_rbufCap));
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
  // A group cue plays this one file (it loops or ends with the cue); the
  // caller's snapshot restores the session's own loop setting afterwards.
  PlayCfg::set(PlaySrc::File, path,
               savePlaylist ? PlayCfg::fileLoop() : PlayFileLoop::One,
               PlayCfg::folderRep(), PlayCfg::folderN(), savePlaylist);
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

void Playback::play() {
  lockPlay();
  if (!s_playing) {
    clearClockLocked();
  }
  s_parked = false;
  s_playing = true;
  s_userPaused = false;
  unlockPlay();
  start();
}

void Playback::pause() {
  lockPlay();
  if (s_playing) {
    clearClockLocked();
  }
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

bool Playback::loopsOnItself() {
  lockPlay();
  const bool on = s_loop && s_listN == 1 && s_passLeft != 1;
  unlockPlay();
  return on;
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
  unlockPlay();
  return true;
}

// Scheduled position in ms since the start of the file's first pass; < 0
// before the cue starts. Caller holds s_mu.
static int64_t schedTotalMsLocked(int64_t masterUs) {
  if (s_sched.paused) {
    return s_sched.pausePosMs;
  }
  return static_cast<int64_t>(s_sched.startPosMs) +
         (masterUs - s_sched.startAtUs) / 1000;
}

// Caller holds s_mu and s_count > 0.
static bool headDueLocked(uint32_t nowMs) {
  const Slot &head = s_ring[s_tail];
  const uint32_t t = head.t_us / 1000u;
  if (s_sched.on) {
    if (s_sched.paused) {
      return false;
    }
    const int64_t total = schedTotalMsLocked(SyncNet::masterUs());
    if (total < 0) {
      return false;
    }
    uint32_t pass = 0;
    uint64_t within = static_cast<uint64_t>(total);
    if (s_sched.loop && s_sched.durMs > 0) {
      pass = static_cast<uint32_t>(within / s_sched.durMs);
      within %= s_sched.durMs;
    }
    const uint16_t p = static_cast<uint16_t>(pass);
    return static_cast<int16_t>(head.pass - p) < 0 ||
           (head.pass == p && t <= within);
  }
  if (!s_clkArmed) {
    return true;
  }
  const int64_t pos = static_cast<int64_t>(nowMs) + s_clkOff;
  if (t + kRewindSlackMs < s_lastShownMs) {
    // Loop back to the top of the show: hold the last frame for one gap.
    s_clkOff = static_cast<int32_t>(static_cast<int64_t>(t) - nowMs -
                                    static_cast<int64_t>(s_lastGapMs));
    s_lastShownMs = t;
    return s_lastGapMs == 0;
  }
  return static_cast<int64_t>(t) <= pos;
}

bool Playback::frameDue(uint32_t nowMs) {
  lockPlay();
  const bool due = s_count > 0 && headDueLocked(nowMs);
  unlockPlay();
  return due;
}

bool Playback::renderDue(uint8_t *rgb, size_t n, uint32_t nowMs) {
  if (!rgb) {
    return false;
  }
  lockPlay();
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
      // Pause the local clock; the next frame re-anchors (never invent
      // frames). A group schedule keeps running on the network clock.
      s_clkArmed = false;
    }
    unlockPlay();
    return false;
  }
  if (!s_sched.on && !s_clkArmed) {
    s_clkOff = static_cast<int32_t>(
        static_cast<int64_t>(s_ring[s_tail].t_us / 1000u) - nowMs);
    s_clkArmed = true;
  }
  const Slot *last = nullptr;
  while (s_count > 0 && headDueLocked(nowMs)) {
    last = &s_ring[s_tail];
    s_tail = static_cast<uint8_t>((s_tail + 1) % s_slots);
    s_count = static_cast<uint8_t>(s_count - 1);
    const uint32_t t = last->t_us / 1000u;
    if (t > s_lastShownMs && t - s_lastShownMs < 1000) {
      s_lastGapMs = t - s_lastShownMs;
    }
    s_lastShownMs = t;
  }
  if (last == nullptr || n < last->size) {
    unlockPlay();
    return false;
  }
  // The slot stays intact until the reader wraps onto it; copy under lock.
  memcpy(rgb, last->rgb, last->size);
  s_tUs = last->t_us;
  s_outFrame = last->frame;
  s_underrun = false;
  unlockPlay();
  return true;
}

uint32_t Playback::showPosMs(uint32_t nowMs) {
  lockPlay();
  uint32_t pos = s_lastShownMs;
  if (s_sched.on) {
    const int64_t total = schedTotalMsLocked(SyncNet::masterUs());
    pos = total < 0 ? 0 : static_cast<uint32_t>(total);
    if (s_sched.loop && s_sched.durMs > 0) {
      pos %= s_sched.durMs;
    }
  } else if (s_clkArmed) {
    const int64_t p = static_cast<int64_t>(nowMs) + s_clkOff;
    pos = p > 0 ? static_cast<uint32_t>(p) : 0;
  } else if (s_count > 0) {
    pos = s_ring[s_tail].t_us / 1000u;
  }
  unlockPlay();
  return pos;
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
        static_cast<uint8_t>((s_head + s_slots - 1) % s_slots);
    const uint32_t newest = s_ring[newestI].t_us / 1000u;
    if (t_ms >= oldest && t_ms <= newest) {
      unlockPlay();
      return true;
    }
  }
  s_seekKind = SeekKind::TimeMs;
  s_seekMs = t_ms;
  s_seekPass = 0;
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
  s_seekPass = 0;
  s_landedReqFrame = 0xFFFFFFFFu;
  resetRingLocked();
  unlockPlay();
  return true;
}

void Playback::setSchedule(uint32_t startPosMs, int64_t startAtMasterUs,
                           uint32_t durMs, bool loop) {
  lockPlay();
  s_sched.on = true;
  s_sched.paused = false;
  s_sched.loop = loop;
  s_sched.startAtUs = startAtMasterUs;
  s_sched.startPosMs = startPosMs;
  s_sched.durMs = durMs ? durMs : s_durMs;
  s_sched.pausePosMs = startPosMs;
  unlockPlay();
}

void Playback::pauseSchedule(uint32_t posMs) {
  lockPlay();
  if (s_sched.on) {
    s_sched.paused = true;
    s_sched.pausePosMs = posMs;
  }
  unlockPlay();
}

void Playback::clearSchedule() {
  lockPlay();
  s_sched = {};
  unlockPlay();
}

bool Playback::scheduled() { return s_sched.on; }

bool Playback::schedulePaused() { return s_sched.on && s_sched.paused; }

uint32_t Playback::scheduleTotalMs() {
  lockPlay();
  const int64_t total =
      s_sched.on ? schedTotalMsLocked(SyncNet::masterUs()) : 0;
  unlockPlay();
  return total < 0 ? 0 : static_cast<uint32_t>(total);
}

bool Playback::scheduleEnded() {
  lockPlay();
  bool ended = false;
  if (s_sched.on && !s_sched.loop && s_sched.durMs > 0 && !s_sched.paused) {
    ended = schedTotalMsLocked(SyncNet::masterUs()) >
            static_cast<int64_t>(s_sched.durMs);
  }
  unlockPlay();
  return ended;
}

void Playback::rebaseSchedule(int64_t deltaUs) {
  lockPlay();
  if (s_sched.on) {
    s_sched.startAtUs += deltaUs;
  }
  unlockPlay();
}

void Playback::seekSchedule(uint32_t leadMs) {
  lockPlay();
  if (!s_sched.on || !s_hasFile) {
    unlockPlay();
    return;
  }
  const int64_t total =
      schedTotalMsLocked(SyncNet::masterUs()) + static_cast<int64_t>(leadMs);
  uint64_t within = total < 0 ? 0 : static_cast<uint64_t>(total);
  uint32_t pass = 0;
  if (s_sched.loop && s_sched.durMs > 0) {
    pass = static_cast<uint32_t>(within / s_sched.durMs);
    within %= s_sched.durMs;
  }
  s_seekPass = static_cast<uint16_t>(pass);
  s_seekKind = SeekKind::TimeMs;
  s_seekMs = static_cast<uint32_t>(within);
  s_landedReqMs = 0xFFFFFFFFu;
  resetRingLocked();
  unlockPlay();
}

uint32_t Playback::scheduleLagMs() {
  lockPlay();
  uint32_t lag = 0;
  if (s_sched.on && !s_sched.paused && s_count == 0 &&
      s_seekKind == SeekKind::None && !s_seekBusy && s_hasFile) {
    const int64_t total = schedTotalMsLocked(SyncNet::masterUs());
    uint64_t within = total < 0 ? 0 : static_cast<uint64_t>(total);
    if (s_sched.loop && s_sched.durMs > 0) {
      within %= s_sched.durMs;
    }
    const uint64_t shown = s_lastShownMs;
    lag = within > shown ? static_cast<uint32_t>(within - shown) : 0;
  }
  unlockPlay();
  return lag;
}

uint32_t Playback::durationMs() { return s_hasFile ? s_durMs : 0; }

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
