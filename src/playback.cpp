#include "playback.h"

#include "dmxrec.h"
#include "log.h"
#include "pixel_map.h"
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
static constexpr uint32_t kPlayUnderrunLogMs = 5000;
static constexpr uint32_t kPlayDiscoverMs = 1000;
static constexpr uint32_t kPlayTaskStack = 8192;
static constexpr UBaseType_t kPlayTaskPrio = 1;
static constexpr BaseType_t kPlayTaskCore = 1;
static constexpr uint32_t kPlayPathLen = 64;

struct Slot {
  uint32_t t_us;
  uint16_t size;
  uint8_t rgb[kPlayMaxPayload];
};

enum class ReadResult : uint8_t { Ok = 0, Skip = 1, Eof = 2, Fail = 3 };

static SemaphoreHandle_t s_mu = nullptr;
static TaskHandle_t s_task = nullptr;
static File s_file;
static volatile bool s_fileOpen = false;

static Slot s_ring[kPlayRingSlots];
static uint8_t s_head = 0;
static uint8_t s_tail = 0;
static uint8_t s_count = 0;

static char s_path[kPlayPathLen];
static bool s_hasFile = false;
static volatile bool s_run = false;
static bool s_underrun = false;
static bool s_begun = false;
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
static uint32_t s_payload = 0;
static uint32_t s_dmxOff = 0;

static uint32_t s_readTUs = 0;
static uint16_t s_readSize = 0;
static uint8_t s_readRgb[kPlayMaxPayload];

static uint32_t sliceBytes() {
  const PixelMapCfg &m = PixelMap::cfg();
  const uint32_t n =
      static_cast<uint32_t>(m.pixelCount) * m.channelsPerPixel;
  if (n == 0) {
    return 0;
  }
  if (n > kPlayMaxPayload) {
    return kPlayMaxPayload;
  }
  return n;
}

static uint32_t dmxStartOff() {
  const uint16_t ch = PixelMap::cfg().startChannel;
  if (ch == 0) {
    return 0;
  }
  return static_cast<uint32_t>(ch - 1);
}

static bool frameForThisNode(uint32_t universe, uint16_t protocol) {
  const PixelMapCfg &m = PixelMap::cfg();
  if (protocol == kDmxrecProtoArtNet) {
    return universe == m.startArtNetUniverse;
  }
  if (protocol == kDmxrecProtoSacn) {
    return universe == m.startSacnUniverse;
  }
  return false;
}

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

static ReadResult readOneFrame() {
  DmxrecFramePrefix prefix;
  uint8_t dmx[kDmxrecDmxBytes];

  if (!sdRead(&prefix, sizeof(prefix))) {
    lockPlay();
    const bool eof = s_frameIndex >= s_frameCount;
    unlockPlay();
    return eof ? ReadResult::Eof : ReadResult::Fail;
  }
  if (!sdRead(dmx, sizeof(dmx))) {
    return ReadResult::Fail;
  }

  lockPlay();
  const uint32_t index = s_frameIndex;
  const uint32_t frames = s_frameCount;
  const uint32_t want = s_payload;
  const uint32_t off = s_dmxOff;
  s_frameIndex = index + 1;
  unlockPlay();

  if (index + 1 > frames) {
    return ReadResult::Eof;
  }

  if (!frameForThisNode(prefix.universe, prefix.protocol)) {
    return ReadResult::Skip;
  }

  uint32_t take = want;
  if (off >= kDmxrecDmxBytes) {
    take = 0;
  } else if (off + take > kDmxrecDmxBytes) {
    take = kDmxrecDmxBytes - off;
  }

  memset(s_readRgb, 0, sizeof(s_readRgb));
  if (take > 0) {
    memcpy(s_readRgb, dmx + off, take);
  }
  s_readSize = static_cast<uint16_t>(want);
  s_readTUs = prefix.t_ms * 1000u;
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
    s.t_us = s_readTUs;
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
  unlockPlay();

  LOG_V("play", "file=%s frames=%u payload=%u artnet=%u sacn=%u", s_path,
        static_cast<unsigned>(h.frame_count), static_cast<unsigned>(payload),
        static_cast<unsigned>(m.startArtNetUniverse),
        static_cast<unsigned>(m.startSacnUniverse));
  return true;
}

static bool tryBindFile() {
  if (!SdInfo::ok()) {
    return false;
  }
  char path[kPlayPathLen];
  if (!SdInfo::findDmx(path, sizeof(path))) {
    if (!s_loggedNoFile) {
      LOG_V("play", "no .dmx (idle)");
      LOG_V("sd", "no .dmx in / (want /show.dmx or first *.dmx)");
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
  s_frameCount = 0;
  s_loggedNoMatch = false;
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

    lockPlay();
    const bool atEnd = s_frameIndex >= s_frameCount;
    unlockPlay();
    if (atEnd) {
      if (!wrapShow()) {
        lockPlay();
        s_run = false;
        unlockPlay();
      }
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
      if (!wrapShow()) {
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
  LOG_V("play", "ring slots=%u payload=%u (DMXREC; no FastLED)", kPlayRingSlots,
        kPlayMaxPayload);
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
  t_us = s_ring[s_tail].t_us;
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
  n = s.size;
  t_us = s.t_us;
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
  if (n < s.size) {
    unlockPlay();
    return false;
  }
  memcpy(rgb, s.rgb, s.size);
  s_tUs = s.t_us;
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

uint16_t Playback::fps() { return 0; }

uint32_t Playback::payloadBytes() {
  lockPlay();
  const uint32_t n = s_hasFile ? s_payload : 0;
  unlockPlay();
  return n;
}

const char *Playback::path() { return s_hasFile ? s_path : ""; }
