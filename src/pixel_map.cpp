#include "pixel_map.h"

#include "board_profile.h"
#include "log.h"

#include <Preferences.h>
#include <cstring>
#include <strings.h>

namespace {

static constexpr char kPrefsNs[] = "pmap";

struct ChipRow {
  LedChipset id;
  const char *name;
  LedWire wire;
  uint8_t t1;
  uint8_t t2;
  uint8_t t3;
};

static const ChipRow kChips[] = {
    {LedChipset::WS2812B, "ws2812b", LedWire::Clockless, 2, 5, 3},
    {LedChipset::WS2812, "ws2812", LedWire::Clockless, 2, 5, 3},
    {LedChipset::SK6812, "sk6812", LedWire::Clockless, 3, 3, 4},
    {LedChipset::WS2811, "ws2811", LedWire::Clockless, 4, 10, 6},
    {LedChipset::WS2813, "ws2813", LedWire::Clockless, 2, 5, 3},
    {LedChipset::WS2815, "ws2815", LedWire::Clockless, 3, 11, 3},
    {LedChipset::WS2816, "ws2816", LedWire::Clockless, 2, 5, 3},
    {LedChipset::WS2818, "ws2818", LedWire::Clockless, 2, 5, 3},
    {LedChipset::SK6822, "sk6822", LedWire::Clockless, 3, 3, 4},
    {LedChipset::TM1803, "tm1803", LedWire::Clockless, 7, 11, 7},
    {LedChipset::TM1804, "tm1804", LedWire::Clockless, 4, 4, 6},
    {LedChipset::TM1809, "tm1809", LedWire::Clockless, 4, 4, 5},
    {LedChipset::TM1829, "tm1829", LedWire::Clockless, 3, 3, 6},
    {LedChipset::UCS1903, "ucs1903", LedWire::Clockless, 4, 10, 4},
    {LedChipset::UCS1903B, "ucs1903b", LedWire::Clockless, 4, 9, 4},
    {LedChipset::UCS1904, "ucs1904", LedWire::Clockless, 4, 4, 5},
    {LedChipset::UCS2903, "ucs2903", LedWire::Clockless, 3, 8, 3},
    {LedChipset::APA106, "apa106", LedWire::Clockless, 4, 4, 6},
    {LedChipset::PL9823, "pl9823", LedWire::Clockless, 4, 10, 4},
    {LedChipset::SM16703, "sm16703", LedWire::Clockless, 3, 6, 3},
    {LedChipset::GE8822, "ge8822", LedWire::Clockless, 4, 7, 4},
    {LedChipset::GW6205, "gw6205", LedWire::Clockless, 4, 4, 4},
    {LedChipset::GS1903, "gs1903", LedWire::Clockless, 3, 9, 6},
    {LedChipset::LPD1886, "lpd1886", LedWire::Clockless, 2, 2, 6},
    {LedChipset::APA102, "apa102", LedWire::Apa102, 0, 0, 0},
    {LedChipset::SK9822, "sk9822", LedWire::Apa102, 0, 0, 0},
    {LedChipset::HD107S, "hd107s", LedWire::Apa102, 0, 0, 0},
    {LedChipset::WS2801, "ws2801", LedWire::Ws2801, 0, 0, 0},
    {LedChipset::LPD8806, "lpd8806", LedWire::Lpd8806, 0, 0, 0},
    {LedChipset::P9813, "p9813", LedWire::P9813, 0, 0, 0},
    {LedChipset::LPD6803, "lpd6803", LedWire::Lpd6803, 0, 0, 0},
};

static PixelMapCfg s_cfg = kMatrixPixelMap;
static bool s_loaded = false;

static const ChipRow *findChip(LedChipset id) {
  for (size_t i = 0; i < sizeof(kChips) / sizeof(kChips[0]); ++i) {
    if (kChips[i].id == id) {
      return &kChips[i];
    }
  }
  return nullptr;
}

static bool validChip(LedChipset chip) { return findChip(chip) != nullptr; }

static void lowerCopy(char *dst, size_t dstLen, const char *s) {
  size_t n = 0;
  if (s != nullptr) {
    while (s[n] != '\0' && n + 1 < dstLen) {
      char c = s[n];
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
      dst[n] = c;
      ++n;
    }
  }
  dst[n] = '\0';
}

static uint8_t chPer(bool white, bool cct) {
  return static_cast<uint8_t>(kRgbChannels + (white ? 1 : 0) + (cct ? 1 : 0));
}

static bool validOrderStr(const char *s, bool white, bool cct) {
  if (s == nullptr || s[0] == '\0') {
    return false;
  }
  char buf[6];
  lowerCopy(buf, sizeof(buf), s);
  const uint8_t want = chPer(white, cct);
  if (strlen(buf) != want) {
    return false;
  }
  bool seen[256] = {};
  uint8_t rgb = 0;
  bool hasW = false;
  bool hasC = false;
  for (uint8_t i = 0; i < want; ++i) {
    const uint8_t c = static_cast<uint8_t>(buf[i]);
    if (seen[c]) {
      return false;
    }
    seen[c] = true;
    if (c == 'r' || c == 'g' || c == 'b') {
      ++rgb;
    } else if (c == 'w') {
      hasW = true;
    } else if (c == 'c') {
      hasC = true;
    } else {
      return false;
    }
  }
  return rgb == 3 && hasW == white && hasC == cct;
}

static void defaultOrder(char out[6], bool white, bool cct) {
  if (white && cct) {
    memcpy(out, "grbwc", 6);
  } else if (white) {
    memcpy(out, "grbw", 5);
    out[4] = '\0';
    out[5] = '\0';
  } else if (cct) {
    memcpy(out, "grbc", 5);
    out[4] = '\0';
    out[5] = '\0';
  } else {
    memcpy(out, "grb", 4);
    out[3] = '\0';
    out[4] = '\0';
    out[5] = '\0';
  }
}

static const char *legacyOrderName(uint8_t v) {
  switch (v) {
  case 1:
    return "rgb";
  case 2:
    return "rbg";
  case 3:
    return "gbr";
  case 4:
    return "brg";
  case 5:
    return "bgr";
  default:
    return "grb";
  }
}

static void applyDerived() {
  s_cfg.channelsPerPixel = chPer(s_cfg.white, s_cfg.cct);
  s_cfg.chipsPerPixel = s_cfg.channelsPerPixel;
  s_cfg.startSacnUniverse =
      static_cast<uint16_t>(s_cfg.startArtNetUniverse + 1);
  const uint32_t spanCh = static_cast<uint32_t>(s_cfg.startChannel - 1) +
                          static_cast<uint32_t>(s_cfg.pixelCount) *
                              s_cfg.channelsPerPixel;
  s_cfg.splitAcrossUniverses = spanCh > kDmxUniverseSize;
}

static PixelChan makeChan(const PixelMapCfg &m, uint16_t uniOff, uint16_t ch) {
  PixelChan c;
  c.artNetUniverse = static_cast<uint16_t>(m.startArtNetUniverse + uniOff);
  c.sacnUniverse = static_cast<uint16_t>(m.startSacnUniverse + uniOff);
  c.channel = ch;
  return c;
}

static bool mapPacked(const PixelMapCfg &m, uint16_t pixelIndex,
                      uint16_t &uniOff, uint16_t &ch1) {
  const uint32_t base = static_cast<uint32_t>(m.startChannel - 1) +
                        static_cast<uint32_t>(pixelIndex) * m.channelsPerPixel;
  uniOff = static_cast<uint16_t>(base / kDmxUniverseSize);
  ch1 = static_cast<uint16_t>(base % kDmxUniverseSize) + 1;
  return uniOff < kMaxUniverses;
}

static bool mapWholePixels(const PixelMapCfg &m, uint16_t pixelIndex,
                           uint16_t &uniOff, uint16_t &ch1) {
  const uint8_t chPerPx = m.channelsPerPixel;
  const uint16_t firstSlots =
      static_cast<uint16_t>(kDmxUniverseSize - (m.startChannel - 1));
  const uint16_t pixFirst = static_cast<uint16_t>(firstSlots / chPerPx);
  if (pixelIndex < pixFirst) {
    uniOff = 0;
    ch1 = static_cast<uint16_t>(m.startChannel +
                                static_cast<uint16_t>(pixelIndex) * chPerPx);
    return true;
  }
  const uint16_t pixPerUni = static_cast<uint16_t>(kDmxUniverseSize / chPerPx);
  if (pixPerUni == 0) {
    return false;
  }
  const uint16_t rem = static_cast<uint16_t>(pixelIndex - pixFirst);
  uniOff = static_cast<uint16_t>(1 + rem / pixPerUni);
  ch1 = static_cast<uint16_t>(1 + (rem % pixPerUni) * chPerPx);
  return uniOff < kMaxUniverses;
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("pmap", "nvs open failed");
    return;
  }
  prefs.putUChar("chip", static_cast<uint8_t>(s_cfg.chipset));
  prefs.putString("ords", s_cfg.colorOrder);
  prefs.putUChar("data", s_cfg.dataGpio);
  prefs.putUChar("clk", s_cfg.clockGpio);
  prefs.putUShort("count", s_cfg.pixelCount);
  prefs.putUShort("uni", s_cfg.startArtNetUniverse);
  prefs.putUShort("ch", s_cfg.startChannel);
  prefs.putUChar("white", s_cfg.white ? 1 : 0);
  prefs.putUChar("cct", s_cfg.cct ? 1 : 0);
  prefs.end();
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  s_cfg = kMatrixPixelMap;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    applyDerived();
    return;
  }
  const uint8_t chip = prefs.getUChar("chip", 0xFF);
  const String ords = prefs.getString("ords", "");
  const uint8_t legacyOrder = prefs.getUChar("order", 0xFF);
  const uint8_t data = prefs.getUChar("data", 0xFF);
  const uint8_t clk = prefs.getUChar("clk", 0xFF);
  const uint16_t count = prefs.getUShort("count", 0);
  const uint16_t uni = prefs.getUShort("uni", 0xFFFF);
  const uint16_t ch = prefs.getUShort("ch", 0);
  const uint8_t white = prefs.getUChar("white", 0xFF);
  const uint8_t cct = prefs.getUChar("cct", 0xFF);
  prefs.end();

  if (chip != 0xFF && validChip(static_cast<LedChipset>(chip))) {
    s_cfg.chipset = static_cast<LedChipset>(chip);
  }
  if (white != 0xFF) {
    s_cfg.white = white != 0;
  }
  if (cct != 0xFF) {
    s_cfg.cct = cct != 0;
  }
  if (ords.length()) {
    char tmp[6];
    lowerCopy(tmp, sizeof(tmp), ords.c_str());
    if (validOrderStr(tmp, s_cfg.white, s_cfg.cct)) {
      memcpy(s_cfg.colorOrder, tmp, sizeof(s_cfg.colorOrder));
    } else {
      defaultOrder(s_cfg.colorOrder, s_cfg.white, s_cfg.cct);
    }
  } else if (legacyOrder != 0xFF) {
    lowerCopy(s_cfg.colorOrder, sizeof(s_cfg.colorOrder),
              legacyOrderName(legacyOrder));
    if (!validOrderStr(s_cfg.colorOrder, s_cfg.white, s_cfg.cct)) {
      defaultOrder(s_cfg.colorOrder, s_cfg.white, s_cfg.cct);
    }
  }
  if (data != 0xFF && PixelMap::validDataGpio(data)) {
    s_cfg.dataGpio = data;
  }
  if (clk != 0xFF) {
    s_cfg.clockGpio = clk;
  }
  if (PixelMap::validCount(count)) {
    s_cfg.pixelCount = count;
  }
  if (uni != 0xFFFF && uni <= 32767) {
    s_cfg.startArtNetUniverse = uni;
  }
  if (ch >= 1 && ch <= kDmxUniverseSize) {
    s_cfg.startChannel = ch;
  }
  applyDerived();
}

static void logCfg() {
  const PixelMapCfg &m = s_cfg;
  LOG_V("pmap",
        "chip=%s order=%s data=%u clk=%u count=%u artnet=%u sacn=%u "
        "start_ch=%u ch_px=%u white=%u cct=%u split=%u",
        PixelMap::chipsetName(), PixelMap::colorOrderName(), m.dataGpio,
        m.clockGpio, m.pixelCount, m.startArtNetUniverse, m.startSacnUniverse,
        m.startChannel, m.channelsPerPixel, m.white ? 1u : 0u, m.cct ? 1u : 0u,
        static_cast<unsigned>(m.splitAcrossUniverses));
}

} // namespace

void PixelMap::begin() {
  loadNvs();
  logCfg();
}

const PixelMapCfg &PixelMap::cfg() {
  loadNvs();
  return s_cfg;
}

uint16_t PixelMap::channelCount() {
  const PixelMapCfg &m = cfg();
  return static_cast<uint16_t>(m.pixelCount * m.channelsPerPixel);
}

uint16_t PixelMap::universeSpan() {
  const PixelMapCfg &m = cfg();
  if (m.pixelCount == 0) {
    return 1;
  }
  uint16_t uniOff = 0;
  uint16_t ch1 = 1;
  if (!pixelOrigin(static_cast<uint16_t>(m.pixelCount - 1), uniOff, ch1)) {
    return 1;
  }
  uint16_t span = static_cast<uint16_t>(uniOff + 1);
  if (span > kMaxUniverses) {
    span = kMaxUniverses;
  }
  if (span == 0) {
    span = 1;
  }
  return span;
}

uint16_t PixelMap::firstUniversePixels() {
  const PixelMapCfg &m = cfg();
  const uint8_t n = m.channelsPerPixel;
  if (n == 0 || m.startChannel == 0 || m.startChannel > kDmxUniverseSize) {
    return 0;
  }
  const uint16_t slots =
      static_cast<uint16_t>(kDmxUniverseSize - (m.startChannel - 1));
  return static_cast<uint16_t>(slots / n);
}

const char *PixelMap::chipsetName() {
  const ChipRow *row = findChip(cfg().chipset);
  return row != nullptr ? row->name : "ws2812b";
}

const char *PixelMap::colorOrderName() {
  loadNvs();
  return s_cfg.colorOrder;
}

LedWire PixelMap::wireKind() {
  const ChipRow *row = findChip(cfg().chipset);
  return row != nullptr ? row->wire : LedWire::Clockless;
}

bool PixelMap::needsClock(LedChipset chip) {
  const ChipRow *row = findChip(chip);
  return row != nullptr && row->wire != LedWire::Clockless;
}

bool PixelMap::needsClock() { return needsClock(cfg().chipset); }

bool PixelMap::clocklessUnits(uint8_t &t1, uint8_t &t2, uint8_t &t3) {
  const ChipRow *row = findChip(cfg().chipset);
  if (row == nullptr || row->wire != LedWire::Clockless) {
    return false;
  }
  t1 = row->t1;
  t2 = row->t2;
  t3 = row->t3;
  return true;
}

bool PixelMap::parseChipset(const char *s, LedChipset &out) {
  if (s == nullptr || s[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < sizeof(kChips) / sizeof(kChips[0]); ++i) {
    if (strcasecmp(s, kChips[i].name) == 0) {
      out = kChips[i].id;
      return true;
    }
  }
  return false;
}

bool PixelMap::validOrder(const char *s, bool white, bool cct) {
  return validOrderStr(s, white, cct);
}

bool PixelMap::parseOrder(const char *s, char out[6], bool white, bool cct) {
  if (!validOrderStr(s, white, cct) || out == nullptr) {
    return false;
  }
  lowerCopy(out, 6, s);
  return true;
}

bool PixelMap::validCount(uint16_t n) { return n >= 1 && n <= kLedCountMax; }

bool PixelMap::validDataGpio(uint8_t pin) {
  if (pin > kS3GpioMax) {
    return false;
  }
  if (pin == 19 || pin == 20) {
    return false;
  }
  if (pin >= 26 && pin <= 32) {
    return false;
  }
  if (pin == BoardProfile::sdCs() || pin == BoardProfile::sdMosi() ||
      pin == BoardProfile::sdClk() || pin == BoardProfile::sdMiso()) {
    return false;
  }
  return true;
}

bool PixelMap::validClockGpio(uint8_t pin, uint8_t dataGpio, bool required) {
  if (!required) {
    return pin == kClockGpioNone ||
           (pin != dataGpio && validDataGpio(pin));
  }
  return pin != kClockGpioNone && pin != dataGpio && validDataGpio(pin);
}

bool PixelMap::set(const PixelMapSet &in, bool save) {
  if (!validChip(in.chipset) || !validCount(in.pixelCount) ||
      !validDataGpio(in.dataGpio) ||
      !validClockGpio(in.clockGpio, in.dataGpio, needsClock(in.chipset)) ||
      in.startChannel < 1 || in.startChannel > kDmxUniverseSize ||
      in.startArtNetUniverse > 32767 ||
      !validOrderStr(in.colorOrder, in.white, in.cct)) {
    return false;
  }
  loadNvs();
  const bool changed =
      in.chipset != s_cfg.chipset ||
      strcmp(in.colorOrder, s_cfg.colorOrder) != 0 ||
      in.dataGpio != s_cfg.dataGpio || in.clockGpio != s_cfg.clockGpio ||
      in.pixelCount != s_cfg.pixelCount ||
      in.startArtNetUniverse != s_cfg.startArtNetUniverse ||
      in.startChannel != s_cfg.startChannel || in.white != s_cfg.white ||
      in.cct != s_cfg.cct;
  s_cfg.chipset = in.chipset;
  memcpy(s_cfg.colorOrder, in.colorOrder, sizeof(s_cfg.colorOrder));
  s_cfg.dataGpio = in.dataGpio;
  s_cfg.clockGpio = needsClock(in.chipset) ? in.clockGpio : kClockGpioNone;
  s_cfg.pixelCount = in.pixelCount;
  s_cfg.startArtNetUniverse = in.startArtNetUniverse;
  s_cfg.startChannel = in.startChannel;
  s_cfg.white = in.white;
  s_cfg.cct = in.cct;
  applyDerived();
  if (save) {
    saveNvs();
  }
  if (changed) {
    logCfg();
  }
  return true;
}

bool PixelMap::pixelOrigin(uint16_t pixelIndex, uint16_t &uniOff,
                           uint16_t &ch1) {
  const PixelMapCfg &m = cfg();
  if (pixelIndex >= m.pixelCount || m.channelsPerPixel < kRgbChannels ||
      m.startChannel == 0 || m.startChannel > kDmxUniverseSize) {
    return false;
  }
  if (m.splitAcrossUniverses) {
    return mapPacked(m, pixelIndex, uniOff, ch1);
  }
  return mapWholePixels(m, pixelIndex, uniOff, ch1);
}

bool PixelMap::lookupRgb(uint16_t pixelIndex, PixelRgbAddr &out) {
  uint16_t uniOff = 0;
  uint16_t ch1 = 1;
  if (!pixelOrigin(pixelIndex, uniOff, ch1)) {
    return false;
  }
  const PixelMapCfg &m = cfg();
  out.r = makeChan(m, uniOff, ch1);
  out.g = makeChan(m, uniOff, static_cast<uint16_t>(ch1 + 1));
  out.b = makeChan(m, uniOff, static_cast<uint16_t>(ch1 + 2));
  return true;
}
