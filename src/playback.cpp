#include "playback.h"

#include "log.h"
#include "pixel_map.h"
#include "rec_format.h"
#include "sd_info.h"

#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

static constexpr uint32_t kSdLockForever = 0xFFFFFFFFu;
static constexpr uint32_t kPlayCrcLogMs = 5000;
static constexpr uint32_t kPlayUnderrunLogMs = 5000;
static constexpr uint32_t kPlayDiscoverMs = 1000;
static constexpr uint32_t kPlayMaxSkip = 4096;
static constexpr uint32_t kPlayTaskStack = 8192;
static constexpr UBaseType_t kPlayTaskPrio = 1;
static constexpr BaseType_t kPlayTaskCore = 1;
static constexpr uint32_t kPlayPathLen = 64;

struct Slot {
  RecFramePrefix prefix;
  uint8_t rgb[kPlayMaxPayload];
};

enum class ReadResult : uint8_t { Ok = 0, Drop = 1, Eof = 2, Fail = 3 };

static SemaphoreHandle_t s_mu = nullptr;
static TaskHandle_t s_task = nullptr;
static File s_file;
static volatile bool s_fileOpen = false;

static Slot s_ring[kPlayRingSlots];
static uint8_t s_head = 0;
static uint8_t s_tail = 0;
static uint8_t s_count = 0;

static RecFileHeader s_hdr;
static char s_path[kPlayPathLen];
static bool s_hasFile = false;
static volatile bool s_run = false;
static bool s_underrun = false;
static bool s_begun = false;
static bool s_loggedNoFile = false;
static bool s_loggedLoop = false;
static bool s_loggedCrc = false;
static uint32_t s_lastCrcLog = 0;
static uint32_t s_lastUnderrunLog = 0;
static uint32_t s_lastDiscover = 0;
static uint32_t s_off = 0;
static uint32_t s_frameIndex = 0;
static uint32_t s_tUs = 0;
static uint32_t s_payload = 0;

static RecFramePrefix s_readPrefix;
static uint8_t s_readRgb[kPlayMaxPayload];

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

static bool mapMatches(const RecFileHeader &h, const char *&why) {
  const PixelMapCfg &m = PixelMap::cfg();
  if (h.pixel_count != m.pixelCount) {
    why = "pixel_count";
    return false;
  }
  if (h.chips_per_pixel != m.channelsPerPixel) {
    why = "chips_per_pixel";
    return false;
  }
  const bool rgbw = (h.flags & kRecFlagRgbw) != 0;
  if (rgbw != (m.channelsPerPixel == 4)) {
    why = "rgbw";
    return false;
  }
  if (h.map_kind == kRecMapIdentity) {
    if (h.map_bytes != 0) {
      why = "identity_map_bytes";
      return false;
    }
    if (h.start_universe != m.startArtNetUniverse &&
        h.start_universe != m.startSacnUniverse) {
      why = "start_universe";
      return false;
    }
    if (m.startChannel == 0 ||
        h.start_channel != static_cast<uint16_t>(m.startChannel - 1)) {
      why = "start_channel";
      return false;
    }
    if ((h.split_universes != 0) != m.splitAcrossUniverses) {
      why = "split";
      return false;
    }
  } else if (h.map_kind == kRecMapCustom) {
    const uint32_t need =
        h.pixel_count * static_cast<uint32_t>(sizeof(RecMapEntry));
    if (h.map_bytes != need) {
      why = "custom_map_bytes";
      return false;
    }
  } else {
    why = "map_kind";
    return false;
  }
  return true;
}

static bool headerOk(const RecFileHeader &h, uint32_t fileBytes,
                     const char *&why) {
  if (!recMagicOk(h)) {
    why = "magic";
    return false;
  }
  if (!recVersionOk(h)) {
    why = "version";
    return false;
  }
  if (h.fps == 0) {
    why = "fps";
    return false;
  }
  if (h.frame_count == 0) {
    why = "frame_count";
    return false;
  }
  if (recHeaderCrc32(h) != h.header_crc32) {
    why = "header_crc";
    return false;
  }
  if (h.pixel_count > kPlayMaxPixels || h.chips_per_pixel > kPlayMaxChips) {
    why = "too_large";
    return false;
  }
  if (!mapMatches(h, why)) {
    return false;
  }
  const uint32_t payload = recPayloadBytes(h);
  if (payload == 0 || payload > kPlayMaxPayload) {
    why = "payload";
    return false;
  }
  const uint32_t dataOff = recFrameDataOffset(h);
  const uint32_t one = recFrameOnDiskBytes(payload);
  if (one == 0 || h.frame_count > (0xFFFFFFFFu - dataOff) / one) {
    why = "size";
    return false;
  }
  const uint32_t need = dataOff + one * h.frame_count;
  if (fileBytes < need) {
    why = "truncated";
    return false;
  }
  return true;
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

static bool sdSkip(uint32_t n) {
  if (!s_fileOpen) {
    return false;
  }
  if (!SdInfo::lock(1000)) {
    return false;
  }
  const uint32_t pos = s_file.position();
  const bool ok = s_file.seek(pos + n);
  if (ok) {
    s_off = s_file.position();
  }
  SdInfo::unlock();
  return ok;
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

static void logCrc(const char *why) {
  const uint32_t now = millis();
  if (s_loggedCrc && now - s_lastCrcLog < kPlayCrcLogMs) {
    return;
  }
  s_loggedCrc = true;
  s_lastCrcLog = now;
  LOG_C("play", "crc drop %s", why);
}

static ReadResult readOneFrame() {
  if (!sdRead(&s_readPrefix, sizeof(s_readPrefix))) {
    lockPlay();
    const bool eof = s_frameIndex >= s_hdr.frame_count;
    unlockPlay();
    return eof ? ReadResult::Eof : ReadResult::Fail;
  }

  lockPlay();
  const uint32_t expected = s_payload;
  const uint32_t frames = s_hdr.frame_count;
  uint32_t index = s_frameIndex;
  unlockPlay();

  if (s_readPrefix.size != expected) {
    logCrc("size");
    if (s_readPrefix.size > kPlayMaxSkip) {
      return ReadResult::Fail;
    }
    if (!sdSkip(s_readPrefix.size + kRecFrameCrcBytes)) {
      return ReadResult::Fail;
    }
    lockPlay();
    s_frameIndex = index + 1;
    unlockPlay();
    if (index + 1 >= frames) {
      return ReadResult::Eof;
    }
    return ReadResult::Drop;
  }

  if (s_readPrefix.size > kPlayMaxPayload) {
    logCrc("payload");
    return ReadResult::Fail;
  }

  if (!sdRead(s_readRgb, s_readPrefix.size)) {
    return ReadResult::Fail;
  }

  uint32_t diskCrc = 0;
  if (!sdRead(&diskCrc, sizeof(diskCrc))) {
    return ReadResult::Fail;
  }

  const uint32_t crc = recFrameCrc32(s_readPrefix, s_readRgb);
  lockPlay();
  s_frameIndex = index + 1;
  unlockPlay();

  if (crc != diskCrc) {
    logCrc("frame");
    if (index + 1 >= frames) {
      return ReadResult::Eof;
    }
    return ReadResult::Drop;
  }

  return ReadResult::Ok;
}

static bool ringFull() {
  lockPlay();
  const bool full = s_count >= kPlayRingSlots;
  unlockPlay();
  return full;
}

static void pushReadFrame() {
  lockPlay();
  if (s_count < kPlayRingSlots) {
    Slot &s = s_ring[s_head];
    s.prefix = s_readPrefix;
    memcpy(s.rgb, s_readRgb, s_readPrefix.size);
    s_head = static_cast<uint8_t>((s_head + 1) % kPlayRingSlots);
    s_count = static_cast<uint8_t>(s_count + 1);
  }
  unlockPlay();
}

static bool wrapShow() {
  lockPlay();
  const uint32_t start = recFrameDataOffset(s_hdr);
  s_frameIndex = 0;
  s_off = start;
  const bool first = !s_loggedLoop;
  s_loggedLoop = true;
  unlockPlay();
  if (first) {
    LOG_V("play", "loop %s", s_path);
  }
  return sdSeek(start);
}

static bool bindPath(const char *path) {
  uint8_t raw[kRecHeaderBytes];
  RecFileHeader h;
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
  const size_t got = f.read(raw, sizeof(raw));
  f.close();
  SdInfo::unlock();

  if (got < 6) {
    LOG_C("play", "reject %s short", path);
    return false;
  }
  if (recIsLegacyDmxrec(raw)) {
    LOG_C("play", "reject %s legacy DMXREC", path);
    return false;
  }
  if (got != sizeof(raw)) {
    LOG_C("play", "reject %s short", path);
    return false;
  }
  memcpy(&h, raw, sizeof(h));

  const char *why = "header";
  if (!headerOk(h, fileBytes, why)) {
    LOG_C("play", "reject %s %s px=%u chips=%u map=%u", path, why,
          static_cast<unsigned>(h.pixel_count),
          static_cast<unsigned>(h.chips_per_pixel),
          static_cast<unsigned>(h.map_kind));
    return false;
  }

  lockPlay();
  s_hdr = h;
  snprintf(s_path, sizeof(s_path), "%s", path);
  s_hasFile = true;
  s_payload = recPayloadBytes(h);
  s_off = recFrameDataOffset(h);
  s_frameIndex = 0;
  s_loggedLoop = false;
  s_loggedCrc = false;
  s_loggedNoFile = false;
  unlockPlay();

  LOG_V("play", "file=%s fps=%u frames=%u px=%u chips=%u payload=%u", s_path,
        static_cast<unsigned>(h.fps), static_cast<unsigned>(h.frame_count),
        static_cast<unsigned>(h.pixel_count),
        static_cast<unsigned>(h.chips_per_pixel),
        static_cast<unsigned>(s_payload));
  if (recHasIndex(h)) {
    LOG_V("play", "index off=%u", static_cast<unsigned>(h.index_offset));
  }
  return true;
}

static bool tryBindFile() {
  if (!SdInfo::ok()) {
    return false;
  }
  char path[kPlayPathLen];
  if (!SdInfo::findDwr(path, sizeof(path))) {
    if (!s_loggedNoFile) {
      LOG_V("play", "no .dwr (idle)");
      LOG_V("sd", "no .dwr in / (want /show.dwr or first *.dwr)");
      s_loggedNoFile = true;
    }
    return false;
  }
  return bindPath(path);
}

static void clearBind() {
  lockPlay();
  s_hasFile = false;
  s_run = false;
  s_path[0] = '\0';
  s_payload = 0;
  resetRingLocked();
  unlockPlay();
}

static void playbackTask(void *) {
  for (;;) {
    lockPlay();
    const bool run = s_run;
    const bool has = s_hasFile;
    unlockPlay();

    if (!run || !has) {
      closeFile();
      vTaskDelay(pdMS_TO_TICKS(50));
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

    const ReadResult r = readOneFrame();
    if (r == ReadResult::Ok) {
      pushReadFrame();
      lockPlay();
      const bool wrapNext = s_frameIndex >= s_hdr.frame_count;
      unlockPlay();
      if (wrapNext && !wrapShow()) {
        LOG_C("play", "loop seek failed");
        lockPlay();
        s_run = false;
        unlockPlay();
      }
      continue;
    }
    if (r == ReadResult::Drop) {
      lockPlay();
      const bool wrapNext = s_frameIndex >= s_hdr.frame_count;
      unlockPlay();
      if (wrapNext && !wrapShow()) {
        LOG_C("play", "loop seek failed");
        lockPlay();
        s_run = false;
        unlockPlay();
      }
      continue;
    }
    if (r == ReadResult::Eof) {
      if (!wrapShow()) {
        LOG_C("play", "loop seek failed");
        lockPlay();
        s_run = false;
        unlockPlay();
      }
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
  LOG_V("play", "ring slots=%u payload=%u (reader fills; no FastLED)",
        kPlayRingSlots, kPlayMaxPayload);
  tryBindFile();
  if (s_task == nullptr) {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(playbackTask, "play", kPlayTaskStack, nullptr,
                                kPlayTaskPrio, &s_task, kPlayTaskCore);
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
    if (s_hasFile || s_run) {
      LOG_V("play", "sd gone");
      clearBind();
    }
    s_loggedNoFile = false;
    return;
  }

  lockPlay();
  const bool has = s_hasFile;
  unlockPlay();
  if (!has && !s_fileOpen) {
    tryBindFile();
  }
}

void Playback::start() {
  if (!s_begun) {
    begin();
  }
  lockPlay();
  bool has = s_hasFile;
  unlockPlay();
  if (!has) {
    has = tryBindFile();
  }
  if (!has) {
    return;
  }
  lockPlay();
  if (s_run) {
    unlockPlay();
    return;
  }
  s_run = true;
  s_underrun = false;
  s_lastUnderrunLog = 0;
  unlockPlay();
  LOG_V("play", "start %s", s_path);
}

void Playback::stop() {
  lockPlay();
  const bool was = s_run;
  s_run = false;
  unlockPlay();
  if (was) {
    LOG_V("play", "stop");
  }
}

bool Playback::hasFile() { return s_hasFile; }

bool Playback::running() { return s_run && s_hasFile; }

bool Playback::underrun() { return s_underrun; }

uint8_t Playback::available() {
  lockPlay();
  const uint8_t n = s_count;
  unlockPlay();
  return n;
}

bool Playback::peek(uint32_t &t_us) {
  lockPlay();
  if (s_count == 0) {
    if (s_run) {
      noteUnderrunLocked();
    }
    unlockPlay();
    return false;
  }
  t_us = s_ring[s_tail].prefix.t_us;
  s_tUs = t_us;
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
  n = s.prefix.size;
  t_us = s.prefix.t_us;
  s_tUs = t_us;
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
  if (n < s.prefix.size) {
    unlockPlay();
    return false;
  }
  memcpy(rgb, s.rgb, s.prefix.size);
  s_tUs = s.prefix.t_us;
  if (t_us) {
    *t_us = s_tUs;
  }
  s_tail = static_cast<uint8_t>((s_tail + 1) % kPlayRingSlots);
  s_count = static_cast<uint8_t>(s_count - 1);
  s_underrun = false;
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

uint16_t Playback::fps() {
  lockPlay();
  const uint16_t fps = s_hasFile ? s_hdr.fps : 0;
  unlockPlay();
  return fps;
}

uint32_t Playback::payloadBytes() {
  lockPlay();
  const uint32_t n = s_hasFile ? s_payload : 0;
  unlockPlay();
  return n;
}

const char *Playback::path() { return s_hasFile ? s_path : ""; }
