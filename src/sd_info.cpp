#include "sd_info.h"

#include "board_matrix.h"
#include "live_input.h"
#include "log.h"

#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

static constexpr uint32_t kSdPollMs = 1000;
static constexpr uint32_t kSdLockForever = 0xFFFFFFFFu;

static bool s_ok = false;
static const char *s_type = "";
static uint32_t s_sizeMb = 0;
static uint32_t s_usedMb = 0;
static uint32_t s_freeMb = 0;
static uint32_t s_lastPoll = 0;
static bool s_loggedFail = false;
static volatile bool s_exclusiveIo = false;
static SemaphoreHandle_t s_mu = nullptr;

static void ensureMu() {
  if (s_mu == nullptr) {
    s_mu = xSemaphoreCreateMutex();
  }
}

static const char *sdCardTypeName(uint8_t cardType) {
  switch (cardType) {
  case CARD_NONE:
    return "none";
  case CARD_MMC:
    return "MMC";
  case CARD_SD:
    return "SDSC";
  case CARD_SDHC:
    return "SDHC";
  default:
    return "unknown";
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

static const char *baseName(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static bool nameIsDwr(const char *name) {
  const char *b = baseName(name);
  const size_t n = strlen(b);
  if (n < 4) {
    return false;
  }
  const char *e = b + n - 4;
  return e[0] == '.' && asciiLower(e[1]) == 'd' && asciiLower(e[2]) == 'w' &&
         asciiLower(e[3]) == 'r';
}

static bool nameIsShowDwr(const char *name) {
  return ieq(baseName(name), "show.dwr");
}

static bool fillPath(char *path, size_t pathLen, const char *name) {
  if (!path || pathLen < 3 || !name || !name[0]) {
    return false;
  }
  if (name[0] == '/') {
    snprintf(path, pathLen, "%s", name);
  } else {
    snprintf(path, pathLen, "/%s", name);
  }
  return path[0] != '\0';
}

static void listSdRoot() {
  File root = SD.open("/");
  if (!root) {
    LOG_C("sd", "root open failed");
    return;
  }

  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    LOG_V("sd", "%s %s bytes=%u", entry.isDirectory() ? "dir" : "file",
          entry.name(), static_cast<unsigned>(entry.size()));
    entry.close();
    ++count;
    if (count >= 12) {
      LOG_V("sd", "root listing truncated");
      break;
    }
  }
  root.close();
  LOG_V("sd", "root entries=%d", count);
}

static void clearCache() {
  s_ok = false;
  s_type = "";
  s_sizeMb = 0;
  s_usedMb = 0;
  s_freeMb = 0;
}

// Caller holds s_mu.
static bool tryMount() {
  if (!SD.begin(kSdCs, SPI, kSdSpiHz)) {
    return false;
  }
  const uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    SD.end();
    return false;
  }

  s_type = sdCardTypeName(cardType);
  const uint64_t card = SD.cardSize();
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  s_sizeMb = static_cast<uint32_t>(card / (1024ULL * 1024ULL));
  s_usedMb = static_cast<uint32_t>(used / (1024ULL * 1024ULL));
  const uint64_t cap = total ? total : card;
  s_freeMb = cap > used
                 ? static_cast<uint32_t>((cap - used) / (1024ULL * 1024ULL))
                 : 0;
  s_ok = true;
  s_loggedFail = false;
  LOG_V("sd", "mount OK type=%s size_MB=%u used_MB=%u free_MB=%u", s_type,
        static_cast<unsigned>(s_sizeMb), static_cast<unsigned>(s_usedMb),
        static_cast<unsigned>(s_freeMb));
  listSdRoot();
  return true;
}

} // namespace

void SdInfo::begin() {
  clearCache();
  ensureMu();
  LOG_V("sd", "spi cs=%u mosi=%u clk=%u miso=%u hz=%u", kSdCs, kSdMosi, kSdClk,
        kSdMiso, static_cast<unsigned>(kSdSpiHz));
  SPI.begin(kSdClk, kSdMiso, kSdMosi, kSdCs);
  if (!lock(kSdLockForever)) {
    LOG_C("sd", "mount failed");
    s_loggedFail = true;
    return;
  }
  const bool mounted = tryMount();
  unlock();
  if (mounted) {
    return;
  }
  LOG_C("sd", "mount failed");
  s_loggedFail = true;
}

void SdInfo::service() {
  if (LiveInput::active()) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastPoll < kSdPollMs) {
    return;
  }
  s_lastPoll = now;

  if (s_exclusiveIo) {
    return;
  }

  if (!lock(200)) {
    return;
  }

  if (s_ok) {
    File root = SD.open("/");
    if (!root) {
      SD.end();
      clearCache();
      unlock();
      LOG_V("sd", "removed");
      return;
    }
    root.close();
    unlock();
    return;
  }

  SD.end();
  const bool ok = tryMount();
  unlock();
  if (ok) {
    return;
  }
  if (!s_loggedFail) {
    LOG_C("sd", "mount failed");
    s_loggedFail = true;
  }
}

bool SdInfo::ok() { return s_ok; }

const char *SdInfo::type() { return s_type; }

uint32_t SdInfo::sizeMb() { return s_sizeMb; }

uint32_t SdInfo::usedMb() { return s_usedMb; }

uint32_t SdInfo::freeMb() { return s_freeMb; }

bool SdInfo::lock(uint32_t waitMs) {
  ensureMu();
  if (!s_mu) {
    return false;
  }
  const TickType_t t =
      (waitMs == kSdLockForever) ? portMAX_DELAY : pdMS_TO_TICKS(waitMs);
  return xSemaphoreTake(s_mu, t) == pdTRUE;
}

void SdInfo::unlock() {
  if (s_mu) {
    xSemaphoreGive(s_mu);
  }
}

void SdInfo::setExclusiveIo(bool on) { s_exclusiveIo = on; }

bool SdInfo::findDwr(char *path, size_t pathLen) {
  if (!path || pathLen < 3 || !s_ok) {
    return false;
  }
  if (!lock(1000)) {
    return false;
  }

  char first[64] = {};
  char show[64] = {};
  File root = SD.open("/");
  if (!root) {
    unlock();
    LOG_C("sd", "root open failed");
    return false;
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory() && nameIsDwr(entry.name())) {
      if (nameIsShowDwr(entry.name())) {
        fillPath(show, sizeof(show), entry.name());
      } else if (first[0] == '\0') {
        fillPath(first, sizeof(first), entry.name());
      }
    }
    entry.close();
    if (show[0] != '\0') {
      break;
    }
  }
  root.close();
  unlock();

  const char *pick = show[0] ? show : (first[0] ? first : nullptr);
  if (!pick) {
    return false;
  }
  snprintf(path, pathLen, "%s", pick);
  return path[0] != '\0';
}
