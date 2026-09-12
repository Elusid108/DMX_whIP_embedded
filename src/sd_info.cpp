#include "sd_info.h"

#include "board_matrix.h"
#include "live_input.h"
#include "log.h"

#include <SD.h>
#include <SPI.h>

namespace {

static constexpr uint32_t kSdPollMs = 1000;

static bool s_ok = false;
static const char *s_type = "";
static uint32_t s_sizeMb = 0;
static uint32_t s_usedMb = 0;
static uint32_t s_freeMb = 0;
static uint32_t s_lastPoll = 0;
static bool s_loggedFail = false;

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
  LOG_V("sd", "spi cs=%u mosi=%u clk=%u miso=%u hz=%u", kSdCs, kSdMosi, kSdClk,
        kSdMiso, static_cast<unsigned>(kSdSpiHz));
  SPI.begin(kSdClk, kSdMiso, kSdMosi, kSdCs);
  if (tryMount()) {
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

  if (s_ok) {
    File root = SD.open("/");
    if (!root) {
      SD.end();
      clearCache();
      LOG_V("sd", "removed");
      return;
    }
    root.close();
    return;
  }

  SD.end();
  if (tryMount()) {
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
