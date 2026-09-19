#include "board_profile.h"

#include "log.h"
#include "pixel_map.h"

#include <Preferences.h>

namespace {

static constexpr char kPrefsNs[] = "board";

static uint8_t s_cs = kSdCs;
static uint8_t s_mosi = kSdMosi;
static uint8_t s_clk = kSdClk;
static uint8_t s_miso = kSdMiso;
static bool s_loaded = false;

static uint8_t loadPin(Preferences &prefs, const char *key, uint8_t fallback) {
  const uint8_t v = prefs.getUChar(key, kGpioUnset);
  if (v == kGpioUnset || !BoardProfile::validGpio(v)) {
    return fallback;
  }
  return v;
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  s_cs = kSdCs;
  s_mosi = kSdMosi;
  s_clk = kSdClk;
  s_miso = kSdMiso;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  s_cs = loadPin(prefs, "sd_cs", kSdCs);
  s_mosi = loadPin(prefs, "sd_mosi", kSdMosi);
  s_clk = loadPin(prefs, "sd_clk", kSdClk);
  s_miso = loadPin(prefs, "sd_miso", kSdMiso);
  prefs.end();
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("board", "nvs open failed");
    return;
  }
  prefs.putUChar("sd_cs", s_cs);
  prefs.putUChar("sd_mosi", s_mosi);
  prefs.putUChar("sd_clk", s_clk);
  prefs.putUChar("sd_miso", s_miso);
  prefs.end();
}

} // namespace

void BoardProfile::begin() {
  loadNvs();
  LOG_V("board", "id=%s chip=%s flash=%s led=%u sd cs=%u mosi=%u clk=%u miso=%u",
        id(), chip(), flashClass(), ledPin(), sdCs(), sdMosi(), sdClk(),
        sdMiso());
}

const char *BoardProfile::id() { return kBoardId; }

const char *BoardProfile::chip() { return kBoardChip; }

const char *BoardProfile::flashClass() { return kBoardFlashClass; }

uint8_t BoardProfile::ledPin() { return PixelMap::cfg().dataGpio; }

uint16_t BoardProfile::ledCount() { return PixelMap::cfg().pixelCount; }

uint8_t BoardProfile::sdCs() {
  loadNvs();
  return s_cs;
}

uint8_t BoardProfile::sdMosi() {
  loadNvs();
  return s_mosi;
}

uint8_t BoardProfile::sdClk() {
  loadNvs();
  return s_clk;
}

uint8_t BoardProfile::sdMiso() {
  loadNvs();
  return s_miso;
}

uint32_t BoardProfile::sdSpiHz() { return kSdSpiHz; }

bool BoardProfile::validGpio(uint8_t pin) { return pin <= kS3GpioMax; }

bool BoardProfile::setSdPins(uint8_t cs, uint8_t mosi, uint8_t clk, uint8_t miso,
                             bool save) {
  if (!validGpio(cs) || !validGpio(mosi) || !validGpio(clk) ||
      !validGpio(miso)) {
    return false;
  }
  if (cs == mosi || cs == clk || cs == miso || mosi == clk || mosi == miso ||
      clk == miso) {
    return false;
  }
  loadNvs();
  s_cs = cs;
  s_mosi = mosi;
  s_clk = clk;
  s_miso = miso;
  if (save) {
    saveNvs();
  }
  LOG_V("board", "sd cs=%u mosi=%u clk=%u miso=%u", s_cs, s_mosi, s_clk,
        s_miso);
  return true;
}
