#include "pixel_map.h"

#include "board_profile.h"
#include "live_cfg.h"
#include "log.h"

#include <Preferences.h>
#include <cstring>
#include <strings.h>

namespace {

static constexpr char kPrefsNs[] = "pmap";
static constexpr uint8_t kBlobVer = 1;

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

#pragma pack(push, 1)
struct SegBlob {
  uint8_t proto;
  uint8_t chip;
  uint8_t data;
  uint8_t clk;
  uint8_t white;
  uint8_t cct;
  uint8_t bri;
  char order[6];
  uint16_t count;
  uint16_t uni;
  uint16_t ch;
};
#pragma pack(pop)

static PixelMapCfg s_seg[kPatchMaxSegments];
static uint8_t s_n = 1;
static uint8_t s_outOf[kPatchMaxSegments];
static uint8_t s_outFirst[kPatchMaxOutputs];
static uint8_t s_outCount[kPatchMaxOutputs];
static uint16_t s_outPxOff[kPatchMaxOutputs];
static uint16_t s_outPxN[kPatchMaxOutputs];
static uint8_t s_nOut = 1;
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

static void applyDerived(PixelMapCfg &m) {
  m.channelsPerPixel = chPer(m.white, m.cct);
  m.chipsPerPixel = m.channelsPerPixel;
  m.startSacnUniverse = static_cast<uint16_t>(m.startArtNetUniverse + 1);
  if (m.proto == SegProto::Sacn && m.startSacnUniverse == 0) {
    m.startSacnUniverse = 1;
    m.startArtNetUniverse = 0;
  }
  const uint32_t spanCh = static_cast<uint32_t>(m.startChannel - 1) +
                          static_cast<uint32_t>(m.pixelCount) *
                              m.channelsPerPixel;
  m.splitAcrossUniverses = spanCh > kDmxUniverseSize;
}

static uint16_t spanOf(const PixelMapCfg &m) {
  if (m.pixelCount == 0 || m.channelsPerPixel == 0 || m.startChannel == 0) {
    return 1;
  }
  const uint32_t last = static_cast<uint32_t>(m.startChannel - 1) +
                        static_cast<uint32_t>(m.pixelCount) * m.channelsPerPixel -
                        1u;
  uint16_t span = static_cast<uint16_t>(last / kDmxUniverseSize + 1);
  if (span == 0) {
    span = 1;
  }
  if (span > kMaxUniverses) {
    span = kMaxUniverses;
  }
  return span;
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

static PixelChan makeChan(const PixelMapCfg &m, uint16_t uniOff, uint16_t ch) {
  PixelChan c;
  c.artNetUniverse = static_cast<uint16_t>(m.startArtNetUniverse + uniOff);
  c.sacnUniverse = static_cast<uint16_t>(m.startSacnUniverse + uniOff);
  c.channel = ch;
  return c;
}

static void rebuildGroups() {
  s_nOut = 0;
  memset(s_outOf, 0, sizeof(s_outOf));
  memset(s_outFirst, 0, sizeof(s_outFirst));
  memset(s_outCount, 0, sizeof(s_outCount));
  memset(s_outPxOff, 0, sizeof(s_outPxOff));
  memset(s_outPxN, 0, sizeof(s_outPxN));
  if (s_n == 0) {
    s_n = 1;
    s_seg[0] = kMatrixPixelMap;
    applyDerived(s_seg[0]);
  }
  for (uint8_t i = 0; i < s_n; ++i) {
    int found = -1;
    for (uint8_t o = 0; o < s_nOut; ++o) {
      if (s_seg[s_outFirst[o]].dataGpio == s_seg[i].dataGpio) {
        found = static_cast<int>(o);
        break;
      }
    }
    if (found < 0) {
      if (s_nOut >= kPatchMaxOutputs) {
        found = 0;
      } else {
        found = static_cast<int>(s_nOut);
        s_outFirst[s_nOut] = i;
        s_outCount[s_nOut] = 0;
        ++s_nOut;
      }
    }
    const uint8_t o = static_cast<uint8_t>(found);
    s_outOf[i] = o;
    ++s_outCount[o];
    const uint8_t parent = s_outFirst[o];
    s_seg[i].chipset = s_seg[parent].chipset;
    s_seg[i].clockGpio = s_seg[parent].clockGpio;
  }
  uint16_t acc = 0;
  for (uint8_t o = 0; o < s_nOut; ++o) {
    s_outPxOff[o] = acc;
    uint16_t n = 0;
    for (uint8_t i = 0; i < s_n; ++i) {
      if (s_outOf[i] == o) {
        n = static_cast<uint16_t>(n + s_seg[i].pixelCount);
      }
    }
    s_outPxN[o] = n;
    acc = static_cast<uint16_t>(acc + n);
  }
}

static bool segOk(const PixelMapCfg &m) {
  if (!validChip(m.chipset) || !PixelMap::validCount(m.pixelCount) ||
      !PixelMap::validDataGpio(m.dataGpio) ||
      !PixelMap::validClockGpio(m.clockGpio, m.dataGpio,
                                PixelMap::needsClock(m.chipset)) ||
      m.startChannel < 1 || m.startChannel > kDmxUniverseSize ||
      m.startArtNetUniverse > 32767 ||
      !validOrderStr(m.colorOrder, m.white, m.cct)) {
    return false;
  }
  if (static_cast<uint8_t>(m.proto) > static_cast<uint8_t>(SegProto::Sacn)) {
    return false;
  }
  return spanOf(m) <= kMaxUniverses;
}

static uint8_t countSlots(const PixelMapCfg *segs, uint8_t n) {
  uint16_t art[kLiveUniSlots];
  uint16_t sac[kLiveUniSlots];
  uint8_t na = 0;
  uint8_t ns = 0;
  auto add = [](uint16_t *arr, uint8_t &c, uint16_t v) {
    for (uint8_t i = 0; i < c; ++i) {
      if (arr[i] == v) {
        return;
      }
    }
    if (c < kLiveUniSlots) {
      arr[c++] = v;
    }
  };
  for (uint8_t i = 0; i < n; ++i) {
    const uint16_t span = spanOf(segs[i]);
    if (segs[i].proto != SegProto::Sacn) {
      for (uint16_t u = 0; u < span; ++u) {
        add(art, na, static_cast<uint16_t>(segs[i].startArtNetUniverse + u));
      }
    }
    if (segs[i].proto != SegProto::ArtNet) {
      for (uint16_t u = 0; u < span; ++u) {
        add(sac, ns, static_cast<uint16_t>(segs[i].startSacnUniverse + u));
      }
    }
  }
  return static_cast<uint8_t>(na + ns);
}

static bool normalize(PixelMapCfg *segs, uint8_t n) {
  if (n < 1 || n > kPatchMaxSegments) {
    return false;
  }
  uint32_t pixels = 0;
  uint8_t outs = 0;
  uint8_t pins[kPatchMaxOutputs];
  for (uint8_t i = 0; i < n; ++i) {
    applyDerived(segs[i]);
    if (!segOk(segs[i])) {
      return false;
    }
    bool seen = false;
    for (uint8_t o = 0; o < outs; ++o) {
      if (pins[o] == segs[i].dataGpio) {
        seen = true;
        for (uint8_t j = 0; j < i; ++j) {
          if (segs[j].dataGpio == segs[i].dataGpio) {
            segs[i].chipset = segs[j].chipset;
            segs[i].clockGpio = segs[j].clockGpio;
            applyDerived(segs[i]);
            break;
          }
        }
        break;
      }
    }
    if (!seen) {
      if (outs >= kPatchMaxOutputs) {
        return false;
      }
      pins[outs++] = segs[i].dataGpio;
    }
    pixels += segs[i].pixelCount;
  }
  if (pixels < 1 || pixels > kLedCountMax) {
    return false;
  }
  for (uint8_t i = 0; i < n; ++i) {
    for (uint8_t j = 0; j < n; ++j) {
      if (i == j) {
        continue;
      }
      if (PixelMap::needsClock(segs[i].chipset) &&
          segs[i].clockGpio == segs[j].dataGpio) {
        return false;
      }
    }
  }
  if (countSlots(segs, n) > kLiveUniSlots) {
    return false;
  }
  return true;
}

static void copySeg0Mirror() {
  // no-op helper marker for save
}

static void saveNvs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("pmap", "nvs open failed");
    return;
  }
  const PixelMapCfg &a = s_seg[0];
  prefs.putUChar("chip", static_cast<uint8_t>(a.chipset));
  prefs.putString("ords", a.colorOrder);
  prefs.putUChar("data", a.dataGpio);
  prefs.putUChar("clk", a.clockGpio);
  prefs.putUShort("count", a.pixelCount);
  prefs.putUShort("uni", a.startArtNetUniverse);
  prefs.putUShort("ch", a.startChannel);
  prefs.putUChar("white", a.white ? 1 : 0);
  prefs.putUChar("cct", a.cct ? 1 : 0);
  prefs.putUChar("proto", static_cast<uint8_t>(a.proto));
  prefs.putUChar("bri", a.brightness);
  prefs.putUChar("n", s_n);
  uint8_t raw[2 + sizeof(SegBlob) * kPatchMaxSegments];
  raw[0] = kBlobVer;
  raw[1] = s_n;
  for (uint8_t i = 0; i < s_n; ++i) {
    SegBlob b;
    memset(&b, 0, sizeof(b));
    b.proto = static_cast<uint8_t>(s_seg[i].proto);
    b.chip = static_cast<uint8_t>(s_seg[i].chipset);
    b.data = s_seg[i].dataGpio;
    b.clk = s_seg[i].clockGpio;
    b.white = s_seg[i].white ? 1 : 0;
    b.cct = s_seg[i].cct ? 1 : 0;
    b.bri = s_seg[i].brightness;
    memcpy(b.order, s_seg[i].colorOrder, sizeof(b.order));
    b.count = s_seg[i].pixelCount;
    b.uni = s_seg[i].startArtNetUniverse;
    b.ch = s_seg[i].startChannel;
    memcpy(raw + 2 + i * sizeof(SegBlob), &b, sizeof(b));
  }
  prefs.putBytes("blob", raw, 2 + s_n * sizeof(SegBlob));
  prefs.end();
  (void)copySeg0Mirror;
}

static bool loadBlob(Preferences &prefs) {
  const size_t need = 2 + sizeof(SegBlob);
  const size_t got = prefs.getBytesLength("blob");
  if (got < need) {
    return false;
  }
  uint8_t raw[2 + sizeof(SegBlob) * kPatchMaxSegments];
  const size_t nread = prefs.getBytes("blob", raw, sizeof(raw));
  if (nread < need || raw[0] != kBlobVer || raw[1] < 1 ||
      raw[1] > kPatchMaxSegments) {
    return false;
  }
  const uint8_t n = raw[1];
  if (nread < 2 + n * sizeof(SegBlob)) {
    return false;
  }
  PixelMapCfg tmp[kPatchMaxSegments];
  for (uint8_t i = 0; i < n; ++i) {
    SegBlob b;
    memcpy(&b, raw + 2 + i * sizeof(SegBlob), sizeof(b));
    tmp[i] = kMatrixPixelMap;
    if (b.proto <= static_cast<uint8_t>(SegProto::Sacn)) {
      tmp[i].proto = static_cast<SegProto>(b.proto);
    }
    if (validChip(static_cast<LedChipset>(b.chip))) {
      tmp[i].chipset = static_cast<LedChipset>(b.chip);
    }
    tmp[i].dataGpio = b.data;
    tmp[i].clockGpio = b.clk;
    tmp[i].white = b.white != 0;
    tmp[i].cct = b.cct != 0;
    tmp[i].brightness = b.bri;
    tmp[i].pixelCount = b.count;
    tmp[i].startArtNetUniverse = b.uni;
    tmp[i].startChannel = b.ch;
    if (validOrderStr(b.order, tmp[i].white, tmp[i].cct)) {
      memcpy(tmp[i].colorOrder, b.order, sizeof(tmp[i].colorOrder));
    } else {
      defaultOrder(tmp[i].colorOrder, tmp[i].white, tmp[i].cct);
    }
    applyDerived(tmp[i]);
  }
  if (!normalize(tmp, n)) {
    return false;
  }
  memcpy(s_seg, tmp, sizeof(PixelMapCfg) * n);
  s_n = n;
  rebuildGroups();
  return true;
}

static void loadLegacy(Preferences &prefs) {
  s_seg[0] = kMatrixPixelMap;
  s_n = 1;
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
  const uint8_t proto = prefs.getUChar("proto", 0xFF);
  const uint8_t bri = prefs.getUChar("bri", 0xFF);

  if (chip != 0xFF && validChip(static_cast<LedChipset>(chip))) {
    s_seg[0].chipset = static_cast<LedChipset>(chip);
  }
  if (white != 0xFF) {
    s_seg[0].white = white != 0;
  }
  if (cct != 0xFF) {
    s_seg[0].cct = cct != 0;
  }
  if (ords.length()) {
    char tmp[6];
    lowerCopy(tmp, sizeof(tmp), ords.c_str());
    if (validOrderStr(tmp, s_seg[0].white, s_seg[0].cct)) {
      memcpy(s_seg[0].colorOrder, tmp, sizeof(s_seg[0].colorOrder));
    } else {
      defaultOrder(s_seg[0].colorOrder, s_seg[0].white, s_seg[0].cct);
    }
  } else if (legacyOrder != 0xFF) {
    lowerCopy(s_seg[0].colorOrder, sizeof(s_seg[0].colorOrder),
              legacyOrderName(legacyOrder));
    if (!validOrderStr(s_seg[0].colorOrder, s_seg[0].white, s_seg[0].cct)) {
      defaultOrder(s_seg[0].colorOrder, s_seg[0].white, s_seg[0].cct);
    }
  }
  if (data != 0xFF && PixelMap::validDataGpio(data)) {
    s_seg[0].dataGpio = data;
  }
  if (clk != 0xFF) {
    s_seg[0].clockGpio = clk;
  }
  if (PixelMap::validCount(count)) {
    s_seg[0].pixelCount = count;
  }
  if (uni != 0xFFFF && uni <= 32767) {
    s_seg[0].startArtNetUniverse = uni;
  }
  if (ch >= 1 && ch <= kDmxUniverseSize) {
    s_seg[0].startChannel = ch;
  }
  if (proto != 0xFF && proto <= static_cast<uint8_t>(SegProto::Sacn)) {
    s_seg[0].proto = static_cast<SegProto>(proto);
  } else {
    s_seg[0].proto = static_cast<SegProto>(LiveCfg::proto());
  }
  if (bri != 0xFF) {
    s_seg[0].brightness = bri;
  }
  applyDerived(s_seg[0]);
  rebuildGroups();
}

static void loadNvs() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  s_seg[0] = kMatrixPixelMap;
  s_n = 1;
  applyDerived(s_seg[0]);
  rebuildGroups();
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  if (!loadBlob(prefs)) {
    loadLegacy(prefs);
  }
  prefs.end();
}

static void logCfg() {
  LOG_V("pmap", "segs=%u outs=%u px=%u proto=%s", s_n, s_nOut,
        PixelMap::totalPixels(), PixelMap::protoSummary());
  for (uint8_t i = 0; i < s_n; ++i) {
    const PixelMapCfg &m = s_seg[i];
    LOG_V("pmap",
          "seg%u out=%u proto=%s chip=%s order=%s data=%u clk=%u count=%u "
          "artnet=%u sacn=%u ch=%u ch_px=%u bri=%u",
          i, s_outOf[i], PixelMap::protoName(m.proto),
          PixelMap::chipsetName(m.chipset), m.colorOrder, m.dataGpio,
          m.clockGpio, m.pixelCount, m.startArtNetUniverse, m.startSacnUniverse,
          m.startChannel, m.channelsPerPixel, m.brightness);
  }
}

} // namespace

void PixelMap::begin() {
  loadNvs();
  logCfg();
}

const PixelMapCfg &PixelMap::cfg() {
  loadNvs();
  return s_seg[0];
}

const PixelMapCfg &PixelMap::segment(uint8_t i) {
  loadNvs();
  if (i >= s_n) {
    return s_seg[0];
  }
  return s_seg[i];
}

uint8_t PixelMap::segmentCount() {
  loadNvs();
  return s_n;
}

uint8_t PixelMap::outputCount() {
  loadNvs();
  return s_nOut;
}

uint8_t PixelMap::firstSegmentOfOutput(uint8_t out) {
  loadNvs();
  if (out >= s_nOut) {
    return 0;
  }
  return s_outFirst[out];
}

uint8_t PixelMap::segmentCountOfOutput(uint8_t out) {
  loadNvs();
  if (out >= s_nOut) {
    return 0;
  }
  return s_outCount[out];
}

uint8_t PixelMap::outputOfSegment(uint8_t seg) {
  loadNvs();
  if (seg >= s_n) {
    return 0;
  }
  return s_outOf[seg];
}

uint16_t PixelMap::outputPixelCount(uint8_t out) {
  loadNvs();
  if (out >= s_nOut) {
    return 0;
  }
  return s_outPxN[out];
}

uint16_t PixelMap::outputPixelOffset(uint8_t out) {
  loadNvs();
  if (out >= s_nOut) {
    return 0;
  }
  return s_outPxOff[out];
}

uint16_t PixelMap::totalPixels() {
  loadNvs();
  uint16_t n = 0;
  for (uint8_t i = 0; i < s_n; ++i) {
    n = static_cast<uint16_t>(n + s_seg[i].pixelCount);
  }
  return n;
}

uint16_t PixelMap::channelCount() { return channelCount(0); }

uint16_t PixelMap::channelCount(uint8_t seg) {
  const PixelMapCfg &m = segment(seg);
  return static_cast<uint16_t>(m.pixelCount * m.channelsPerPixel);
}

uint16_t PixelMap::universeSpan() { return universeSpan(0); }

uint16_t PixelMap::universeSpan(uint8_t seg) {
  loadNvs();
  return spanOf(segment(seg));
}

uint16_t PixelMap::firstUniversePixels() { return firstUniversePixels(0); }

uint16_t PixelMap::firstUniversePixels(uint8_t seg) {
  const PixelMapCfg &m = segment(seg);
  const uint8_t n = m.channelsPerPixel;
  if (n == 0 || m.startChannel == 0 || m.startChannel > kDmxUniverseSize) {
    return 0;
  }
  const uint16_t slots =
      static_cast<uint16_t>(kDmxUniverseSize - (m.startChannel - 1));
  return static_cast<uint16_t>(slots / n);
}

const char *PixelMap::chipsetName() { return chipsetName(cfg().chipset); }

const char *PixelMap::chipsetName(LedChipset chip) {
  const ChipRow *row = findChip(chip);
  return row != nullptr ? row->name : "ws2812b";
}

const char *PixelMap::colorOrderName() { return colorOrderName(0); }

const char *PixelMap::colorOrderName(uint8_t seg) {
  loadNvs();
  return segment(seg).colorOrder;
}

const char *PixelMap::protoName(SegProto proto) {
  switch (proto) {
  case SegProto::ArtNet:
    return "artnet";
  case SegProto::Sacn:
    return "sacn";
  case SegProto::Auto:
  default:
    return "auto";
  }
}

const char *PixelMap::protoSummary() {
  loadNvs();
  bool art = false;
  bool sac = false;
  bool aut = false;
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto == SegProto::ArtNet) {
      art = true;
    } else if (s_seg[i].proto == SegProto::Sacn) {
      sac = true;
    } else {
      aut = true;
    }
  }
  const uint8_t kinds =
      static_cast<uint8_t>((art ? 1 : 0) + (sac ? 1 : 0) + (aut ? 1 : 0));
  if (kinds > 1) {
    return "mixed";
  }
  if (art) {
    return "artnet";
  }
  if (sac) {
    return "sacn";
  }
  return "auto";
}

LedWire PixelMap::wireKind() { return wireKind(cfg().chipset); }

LedWire PixelMap::wireKind(LedChipset chip) {
  const ChipRow *row = findChip(chip);
  return row != nullptr ? row->wire : LedWire::Clockless;
}

bool PixelMap::needsClock(LedChipset chip) {
  const ChipRow *row = findChip(chip);
  return row != nullptr && row->wire != LedWire::Clockless;
}

bool PixelMap::needsClock() { return needsClock(cfg().chipset); }

bool PixelMap::clocklessUnits(uint8_t &t1, uint8_t &t2, uint8_t &t3) {
  return clocklessUnits(cfg().chipset, t1, t2, t3);
}

bool PixelMap::clocklessUnits(LedChipset chip, uint8_t &t1, uint8_t &t2,
                              uint8_t &t3) {
  const ChipRow *row = findChip(chip);
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

bool PixelMap::parseProto(const char *s, SegProto &out) {
  if (s == nullptr) {
    return false;
  }
  if (strcasecmp(s, "auto") == 0) {
    out = SegProto::Auto;
    return true;
  }
  if (strcasecmp(s, "artnet") == 0) {
    out = SegProto::ArtNet;
    return true;
  }
  if (strcasecmp(s, "sacn") == 0) {
    out = SegProto::Sacn;
    return true;
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
    return pin == kClockGpioNone || (pin != dataGpio && validDataGpio(pin));
  }
  return pin != kClockGpioNone && pin != dataGpio && validDataGpio(pin);
}

bool PixelMap::wantsArtNet(uint16_t uni) {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto == SegProto::Sacn) {
      continue;
    }
    const uint16_t start = s_seg[i].startArtNetUniverse;
    const uint16_t span = spanOf(s_seg[i]);
    if (uni >= start && uni < static_cast<uint16_t>(start + span)) {
      return true;
    }
  }
  return false;
}

bool PixelMap::wantsSacn(uint16_t uni) {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto == SegProto::ArtNet) {
      continue;
    }
    const uint16_t start = s_seg[i].startSacnUniverse;
    const uint16_t span = spanOf(s_seg[i]);
    if (uni >= start && uni < static_cast<uint16_t>(start + span)) {
      return true;
    }
  }
  return false;
}

bool PixelMap::anyArtNet() {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto != SegProto::Sacn) {
      return true;
    }
  }
  return false;
}

bool PixelMap::anySacn() {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto != SegProto::ArtNet) {
      return true;
    }
  }
  return false;
}

bool PixelMap::allSacnOnly() {
  loadNvs();
  if (s_n == 0) {
    return false;
  }
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto != SegProto::Sacn) {
      return false;
    }
  }
  return true;
}

uint16_t PixelMap::firstArtNetUniverse() {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto != SegProto::Sacn) {
      return s_seg[i].startArtNetUniverse;
    }
  }
  return s_seg[0].startArtNetUniverse;
}

uint16_t PixelMap::firstSacnUniverse() {
  loadNvs();
  for (uint8_t i = 0; i < s_n; ++i) {
    if (s_seg[i].proto != SegProto::ArtNet) {
      return s_seg[i].startSacnUniverse;
    }
  }
  return s_seg[0].startSacnUniverse;
}

uint8_t PixelMap::collectSacnUniverses(uint16_t *out, uint8_t max) {
  loadNvs();
  uint8_t n = 0;
  if (out == nullptr || max == 0) {
    return 0;
  }
  for (uint8_t i = 0; i < s_n && n < max; ++i) {
    if (s_seg[i].proto == SegProto::ArtNet) {
      continue;
    }
    const uint16_t start = s_seg[i].startSacnUniverse;
    const uint16_t span = spanOf(s_seg[i]);
    for (uint16_t u = 0; u < span && n < max; ++u) {
      const uint16_t uni = static_cast<uint16_t>(start + u);
      bool seen = false;
      for (uint8_t j = 0; j < n; ++j) {
        if (out[j] == uni) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        out[n++] = uni;
      }
    }
  }
  return n;
}

bool PixelMap::set(const PixelMapSet &in, bool save) {
  PixelMapCfg m = cfg();
  m.proto = in.proto;
  m.chipset = in.chipset;
  memcpy(m.colorOrder, in.colorOrder, sizeof(m.colorOrder));
  m.dataGpio = in.dataGpio;
  m.clockGpio = needsClock(in.chipset) ? in.clockGpio : kClockGpioNone;
  m.pixelCount = in.pixelCount;
  m.startArtNetUniverse = in.startArtNetUniverse;
  m.startChannel = in.startChannel;
  m.white = in.white;
  m.cct = in.cct;
  m.brightness = in.brightness;
  applyDerived(m);
  PixelMapCfg tmp[kPatchMaxSegments];
  loadNvs();
  memcpy(tmp, s_seg, sizeof(PixelMapCfg) * s_n);
  tmp[0] = m;
  if (!normalize(tmp, s_n)) {
    return false;
  }
  memcpy(s_seg, tmp, sizeof(PixelMapCfg) * s_n);
  rebuildGroups();
  if (save) {
    saveNvs();
  }
  logCfg();
  return true;
}

bool PixelMap::setAll(const PixelMapCfg *segs, uint8_t n, bool save) {
  if (segs == nullptr) {
    return false;
  }
  PixelMapCfg tmp[kPatchMaxSegments];
  memcpy(tmp, segs, sizeof(PixelMapCfg) * n);
  if (!normalize(tmp, n)) {
    return false;
  }
  loadNvs();
  memcpy(s_seg, tmp, sizeof(PixelMapCfg) * n);
  s_n = n;
  rebuildGroups();
  if (save) {
    saveNvs();
  }
  logCfg();
  return true;
}

bool PixelMap::setAllProtos(SegProto proto, bool save) {
  loadNvs();
  PixelMapCfg tmp[kPatchMaxSegments];
  memcpy(tmp, s_seg, sizeof(PixelMapCfg) * s_n);
  for (uint8_t i = 0; i < s_n; ++i) {
    tmp[i].proto = proto;
    applyDerived(tmp[i]);
  }
  if (!normalize(tmp, s_n)) {
    return false;
  }
  memcpy(s_seg, tmp, sizeof(PixelMapCfg) * s_n);
  rebuildGroups();
  if (save) {
    saveNvs();
  }
  logCfg();
  return true;
}

bool PixelMap::setSegmentBrightness(uint8_t i, uint8_t bri, bool save) {
  loadNvs();
  if (i >= s_n) {
    return false;
  }
  s_seg[i].brightness = bri;
  if (save) {
    saveNvs();
  }
  return true;
}

bool PixelMap::pixelOrigin(uint16_t pixelIndex, uint16_t &uniOff,
                           uint16_t &ch1) {
  return pixelOrigin(0, pixelIndex, uniOff, ch1);
}

bool PixelMap::pixelOrigin(uint8_t seg, uint16_t pixelIndex, uint16_t &uniOff,
                           uint16_t &ch1) {
  const PixelMapCfg &m = segment(seg);
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

bool PixelMap::locatePixel(uint16_t globalIndex, uint8_t &seg, uint16_t &local) {
  loadNvs();
  uint16_t acc = 0;
  for (uint8_t o = 0; o < s_nOut; ++o) {
    for (uint8_t i = 0; i < s_n; ++i) {
      if (s_outOf[i] != o) {
        continue;
      }
      const uint16_t n = s_seg[i].pixelCount;
      if (globalIndex < static_cast<uint16_t>(acc + n)) {
        seg = i;
        local = static_cast<uint16_t>(globalIndex - acc);
        return true;
      }
      acc = static_cast<uint16_t>(acc + n);
    }
  }
  return false;
}
