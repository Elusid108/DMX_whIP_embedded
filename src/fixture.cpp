#include "fixture.h"

#include "led_bus.h"
#include "live_input.h"
#include "log.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "sync.h"
#include "sync_net.h"

#include <LittleFS.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

static constexpr char kCfgPath[] = "/fixture.bin";
static constexpr char kNamesPath[] = "/pxnames.txt";
static constexpr uint8_t kFileVer = 1;
static constexpr uint32_t kClipSettleMs = 100;
static constexpr uint16_t kNoSub = 0xFF;

struct FixRange {
  uint16_t from;
  uint16_t to; // inclusive
};

struct SubCfg {
  char name[kFixNameLen];
  uint16_t firstRange;
  uint8_t nRanges;
};

struct Cfg {
  bool en;
  FixMode mode;
  bool sacn;
  uint16_t uni;
  uint16_t ch;
  uint8_t nSubs;
  uint16_t nRanges;
  SubCfg subs[kFixMaxSubs];
  FixRange ranges[kFixMaxRanges];
};

struct Stage {
  Cfg cfg;
  uint8_t used[kLedCountMax / 8];
};

static Cfg s_cfg = {};
static Stage *s_stage = nullptr;
static bool s_fsOk = false;

// Derived from s_cfg and the main patch.
static bool s_valid = false;
static const char *s_err = "";
static uint16_t s_total = 0;
static uint16_t s_footprint = 0;
static uint8_t s_unis = 1;
static uint32_t s_mapGen = 0xFFFFFFFFu;
static uint8_t s_subOf[kLedCountMax];
static uint16_t s_fullAddr[kLedCountMax];

// Per frame.
static FixDrive s_drive = FixDrive::None;
static const uint8_t *s_data[kMaxUniverses];
static uint8_t s_dim = 255;       // master dimmer (latched), no strobe
static bool s_open = true;        // master strobe gate
static uint8_t s_strobeRgb[3] = {255, 255, 255};
static bool s_strobeWhite = true;
static uint8_t s_strobeInt = 0;   // off-phase level (0 = blackout)
static uint8_t s_subOpen[(kFixMaxSubs + 7) / 8];
static bool s_hueOn = false;
static int16_t s_hue[9];
static bool s_filterOn = false;
static uint8_t s_filter[3] = {255, 255, 255};
static bool s_addOn = false;
static uint8_t s_add[3] = {0, 0, 0};
static uint8_t s_subLevel[kFixMaxSubs]; // sub dimmer x master, no strobe
static uint8_t s_subRgb[kFixMaxSubs][3];
// Dimmers the console has sent above 0 since the fixture universe was last
// heard; the rest stay open (a channel the console does not patch reads 0).
static bool s_masterUsed = false;
static uint8_t s_subUsed[(kFixMaxSubs + 7) / 8];

static void resetDimLatches() {
  s_masterUsed = false;
  memset(s_subUsed, 0, sizeof(s_subUsed));
}

// v, or open (255) until the console has sent this dimmer above 0.
static uint8_t latchDim(uint8_t v, bool &used) {
  used = used || v != 0;
  return used ? v : 255;
}

static uint8_t latchSubDim(uint8_t k, uint8_t v) {
  bool used = (s_subUsed[k >> 3] >> (k & 7)) & 1;
  const uint8_t out = latchDim(v, used);
  if (used) {
    s_subUsed[k >> 3] |= static_cast<uint8_t>(1u << (k & 7));
  }
  return out;
}

// Locate.
static uint8_t s_locate[kLedCountMax / 8];
static uint32_t s_locateUntil = 0;
static bool s_locateOn = false;

// Folder + clip select (folder << 8 | clip).
static uint16_t s_pickSeen = 0;
static uint16_t s_pickActed = 0;
static uint32_t s_pickSince = 0;

static inline uint8_t fxMul(uint8_t a, uint8_t b) {
  return static_cast<uint8_t>((static_cast<uint16_t>(a) * (static_cast<uint16_t>(b) + 1)) >> 8);
}

static inline uint8_t clamp8(int32_t v) {
  return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v));
}

static LiveSource fixSource() { return s_cfg.sacn ? LiveSource::Sacn : LiveSource::ArtNet; }

static uint8_t perSub(FixMode mode) {
  return mode == FixMode::Dim ? 2 : (mode == FixMode::Rgb ? 5 : 0);
}

// Header channel offsets per mode (-1: not in this mode).
struct HdrMap {
  int8_t dim, strobe, scol, sint, hue, filter, add, folder, clip;
  uint8_t len;
};
static constexpr HdrMap kHdrFull = {0, 1, 2, 3, 4, 5, 8, 11, 12, kFixHeader};
static constexpr HdrMap kHdrBasic = {0, 1, -1, -1, 2, -1, -1, 3, 4, kFixHeaderBasic};

static const HdrMap &hdrOf(FixMode mode) {
  return mode == FixMode::Basic ? kHdrBasic : kHdrFull;
}

// Look modes overlay what the node plays; the fixture universe is control.
static bool lookMode(FixMode mode) { return mode == FixMode::Dim || mode == FixMode::Basic; }

// Strobe colour: 0 white, 1-255 round the wheel from red.
static void wheel(uint8_t v, uint8_t *rgb) {
  if (v == 0) {
    rgb[0] = rgb[1] = rgb[2] = 255;
    return;
  }
  const uint16_t h = static_cast<uint16_t>((static_cast<uint32_t>(v - 1) * 1536u) / 255u);
  const uint8_t f = static_cast<uint8_t>(h & 0xFF);
  switch (h >> 8) {
  case 0:
    rgb[0] = 255, rgb[1] = f, rgb[2] = 0;
    break;
  case 1:
    rgb[0] = static_cast<uint8_t>(255 - f), rgb[1] = 255, rgb[2] = 0;
    break;
  case 2:
    rgb[0] = 0, rgb[1] = 255, rgb[2] = f;
    break;
  case 3:
    rgb[0] = 0, rgb[1] = static_cast<uint8_t>(255 - f), rgb[2] = 255;
    break;
  case 4:
    rgb[0] = f, rgb[1] = 0, rgb[2] = 255;
    break;
  default:
    rgb[0] = 255, rgb[1] = 0, rgb[2] = static_cast<uint8_t>(255 - f);
    break;
  }
}

static inline bool subOpen(uint8_t k) { return (s_subOpen[k >> 3] >> (k & 7)) & 1; }

// 0-9 open; 10-255 = 1-25 Hz, 40% on, phased to the shared clock.
static bool strobeOn(uint8_t v, int64_t tUs) {
  if (v < 10) {
    return true;
  }
  const uint32_t tenthsHz = 10u + (static_cast<uint32_t>(v - 10) * 240u) / 245u;
  const uint32_t periodUs = 10000000u / tenthsHz;
  const uint64_t t = tUs < 0 ? 0 : static_cast<uint64_t>(tUs);
  return static_cast<uint32_t>(t % periodUs) < (periodUs * 2u) / 5u;
}

// Luminance-keeping hue rotation, Q10.
static void buildHue(uint8_t v) {
  const float a = static_cast<float>(v) * (6.2831853f / 256.0f);
  const float c = cosf(a);
  const float s = sinf(a);
  const float m[9] = {
      0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f,
      0.072f - c * 0.072f + s * 0.928f, 0.213f - c * 0.213f + s * 0.143f,
      0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f,
      0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f,
      0.072f + c * 0.928f + s * 0.072f};
  for (uint8_t i = 0; i < 9; ++i) {
    s_hue[i] = static_cast<int16_t>(lroundf(m[i] * 1024.0f));
  }
}

static void fxColor(uint8_t *px, uint8_t cpp, uint8_t level) {
  if (cpp < 3) {
    for (uint8_t i = 0; i < cpp; ++i) {
      px[i] = fxMul(px[i], level);
    }
    return;
  }
  uint8_t r = px[0];
  uint8_t g = px[1];
  uint8_t b = px[2];
  if (s_hueOn) {
    const int32_t nr = (s_hue[0] * r + s_hue[1] * g + s_hue[2] * b) >> 10;
    const int32_t ng = (s_hue[3] * r + s_hue[4] * g + s_hue[5] * b) >> 10;
    const int32_t nb = (s_hue[6] * r + s_hue[7] * g + s_hue[8] * b) >> 10;
    r = clamp8(nr);
    g = clamp8(ng);
    b = clamp8(nb);
  }
  if (s_filterOn) {
    r = fxMul(r, s_filter[0]);
    g = fxMul(g, s_filter[1]);
    b = fxMul(b, s_filter[2]);
  }
  if (s_addOn) {
    r = clamp8(r + s_add[0]);
    g = clamp8(g + s_add[1]);
    b = clamp8(b + s_add[2]);
  }
  if (level != 255) {
    r = fxMul(r, level);
    g = fxMul(g, level);
    b = fxMul(b, level);
    for (uint8_t i = 3; i < cpp; ++i) {
      px[i] = fxMul(px[i], level);
    }
  }
  px[0] = r;
  px[1] = g;
  px[2] = b;
}

// A pixel in the strobe's off phase: the strobe colour at the strobe
// intensity, scaled by the pixel's dimmer (0 intensity = blackout).
static void strobePixel(uint8_t *px, uint8_t cpp, uint8_t level) {
  const uint8_t v = fxMul(s_strobeInt, level);
  if (cpp < 3) {
    for (uint8_t i = 0; i < cpp; ++i) {
      px[i] = v;
    }
    return;
  }
  for (uint8_t i = 0; i < 3; ++i) {
    px[i] = fxMul(s_strobeRgb[i], v);
  }
  for (uint8_t i = 3; i < cpp; ++i) {
    px[i] = i == 3 && s_strobeWhite ? v : 0; // white strobe uses W too
  }
}

static void appendEsc(String &out, const char *s) {
  out += '"';
  for (; s && *s; ++s) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

static void appendRanges(String &out, const Cfg &c, const SubCfg &sub) {
  out += '"';
  for (uint8_t i = 0; i < sub.nRanges; ++i) {
    const FixRange &r = c.ranges[sub.firstRange + i];
    if (i) {
      out += ',';
    }
    out += static_cast<unsigned>(r.from);
    if (r.to != r.from) {
      out += '-';
      out += static_cast<unsigned>(r.to);
    }
  }
  out += '"';
}

// ---------------------------------------------------------------- derive

static uint8_t cppOf(uint16_t g) {
  uint8_t seg = 0;
  uint16_t local = 0;
  if (!PixelMap::locatePixel(g, seg, local)) {
    return 3;
  }
  return PixelMap::segment(seg).channelsPerPixel;
}

// Footprint and universes of c on the current main patch; the Full-mode
// address table when tables is set.
static bool derive(const Cfg &c, bool tables, uint16_t &footprint, uint8_t &unis,
                   const char *&err) {
  err = "";
  footprint = 0;
  unis = 1;
  const uint16_t total = PixelMap::totalPixels();
  if (c.ch < 1 || c.ch > kDmxUniverseSize) {
    err = "channel must be 1-512";
    return false;
  }
  if (c.sacn ? (c.uni < 1 || c.uni > 63999) : (c.uni > 32767)) {
    err = "universe out of range";
    return false;
  }
  const uint16_t base = static_cast<uint16_t>(c.ch - 1);
  if (c.mode == FixMode::Full) {
    uint32_t pos = base + hdrOf(c.mode).len;
    if (pos > kDmxUniverseSize) {
      err = "the header does not fit after this channel";
      return false;
    }
    for (uint16_t g = 0; g < total; ++g) {
      const uint8_t cpp = cppOf(g);
      if ((pos % kDmxUniverseSize) + cpp > kDmxUniverseSize) {
        pos = (pos / kDmxUniverseSize + 1) * kDmxUniverseSize;
      }
      if (tables) {
        s_fullAddr[g] = static_cast<uint16_t>(pos);
      }
      pos += cpp;
    }
    unis = static_cast<uint8_t>((pos + kDmxUniverseSize - 1) / kDmxUniverseSize);
    if (unis == 0) {
      unis = 1;
    }
    if (unis > kMaxUniverses) {
      err = "Full mode needs more than 6 universes";
      return false;
    }
    footprint = static_cast<uint16_t>(pos - base);
  } else {
    footprint = static_cast<uint16_t>(hdrOf(c.mode).len + perSub(c.mode) * c.nSubs);
    if (base + footprint > kDmxUniverseSize) {
      err = "the fixture does not fit in the universe from this channel";
      return false;
    }
  }
  if (!c.en) {
    return true;
  }
  if (total == 0) {
    err = "the main patch has no pixels";
    return false;
  }
  uint8_t shared = 0;
  for (uint8_t u = 0; u < unis; ++u) {
    const uint16_t uni = static_cast<uint16_t>(c.uni + u);
    const bool mapToo = c.sacn ? PixelMap::wantsSacn(uni) : PixelMap::wantsArtNet(uni);
    if (mapToo) {
      if (lookMode(c.mode)) {
        err = "Dim and Basic modes need their own universe, apart from the main patch";
        return false;
      }
      ++shared;
    }
  }
  if (PixelMap::usedSlots() + unis - shared > kLiveUniSlots) {
    err = "not enough universe slots left (16 in total)";
    return false;
  }
  return true;
}

static void rebuild() {
  s_mapGen = PixelMap::generation();
  s_total = PixelMap::totalPixels();
  const char *err = "";
  s_valid = derive(s_cfg, true, s_footprint, s_unis, err);
  s_err = err;
  resetDimLatches();
  memset(s_subOf, 0xFF, sizeof(s_subOf));
  for (uint8_t k = 0; k < s_cfg.nSubs; ++k) {
    const SubCfg &sub = s_cfg.subs[k];
    for (uint8_t i = 0; i < sub.nRanges; ++i) {
      const FixRange &r = s_cfg.ranges[sub.firstRange + i];
      for (uint32_t g = r.from; g <= r.to && g < s_total; ++g) {
        s_subOf[g] = k;
      }
    }
  }
  if (s_cfg.en && !s_valid) {
    LOG_C("fix", "disabled: %s", s_err);
  }
}

// ---------------------------------------------------------------- storage

static bool saveCfg(const Cfg &c) {
  if (!s_fsOk) {
    return false;
  }
  File f = LittleFS.open(kCfgPath, "w");
  if (!f) {
    return false;
  }
  uint8_t head[12] = {'W', 'F', 'X', kFileVer,
                      static_cast<uint8_t>(c.en ? 1 : 0),
                      static_cast<uint8_t>(c.mode),
                      static_cast<uint8_t>(c.sacn ? 1 : 0),
                      c.nSubs,
                      static_cast<uint8_t>(c.uni & 0xFF),
                      static_cast<uint8_t>(c.uni >> 8),
                      static_cast<uint8_t>(c.ch & 0xFF),
                      static_cast<uint8_t>(c.ch >> 8)};
  bool ok = f.write(head, sizeof(head)) == sizeof(head);
  for (uint8_t k = 0; ok && k < c.nSubs; ++k) {
    const SubCfg &sub = c.subs[k];
    const uint8_t nameLen = static_cast<uint8_t>(strnlen(sub.name, kFixNameLen - 1));
    ok = f.write(&nameLen, 1) == 1 && f.write(reinterpret_cast<const uint8_t *>(sub.name), nameLen) == nameLen &&
         f.write(&sub.nRanges, 1) == 1;
    for (uint8_t i = 0; ok && i < sub.nRanges; ++i) {
      const FixRange &r = c.ranges[sub.firstRange + i];
      const uint8_t b[4] = {static_cast<uint8_t>(r.from & 0xFF), static_cast<uint8_t>(r.from >> 8),
                            static_cast<uint8_t>(r.to & 0xFF), static_cast<uint8_t>(r.to >> 8)};
      ok = f.write(b, 4) == 4;
    }
  }
  f.close();
  return ok;
}

static void loadCfg() {
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.ch = 1;
  s_cfg.uni = 0;
  if (!s_fsOk || !LittleFS.exists(kCfgPath)) {
    return;
  }
  File f = LittleFS.open(kCfgPath, "r");
  if (!f) {
    return;
  }
  uint8_t head[12];
  bool ok = f.read(head, sizeof(head)) == sizeof(head) && head[0] == 'W' && head[1] == 'F' &&
            head[2] == 'X' && head[3] == kFileVer && head[5] <= 3 && head[7] <= kFixMaxSubs;
  // Too big for the loop task's stack.
  Cfg *tmp = static_cast<Cfg *>(calloc(1, sizeof(Cfg)));
  if (!tmp) {
    f.close();
    return;
  }
  Cfg &c = *tmp;
  if (ok) {
    c.en = head[4] != 0;
    c.mode = static_cast<FixMode>(head[5]);
    c.sacn = head[6] != 0;
    c.nSubs = head[7];
    c.uni = static_cast<uint16_t>(head[8] | (head[9] << 8));
    c.ch = static_cast<uint16_t>(head[10] | (head[11] << 8));
  }
  for (uint8_t k = 0; ok && k < c.nSubs; ++k) {
    SubCfg &sub = c.subs[k];
    uint8_t nameLen = 0;
    ok = f.read(&nameLen, 1) == 1 && nameLen < kFixNameLen &&
         f.read(reinterpret_cast<uint8_t *>(sub.name), nameLen) == nameLen;
    if (!ok) {
      break;
    }
    sub.name[nameLen] = '\0';
    ok = f.read(&sub.nRanges, 1) == 1 && c.nRanges + sub.nRanges <= kFixMaxRanges;
    sub.firstRange = c.nRanges;
    for (uint8_t i = 0; ok && i < sub.nRanges; ++i) {
      uint8_t b[4];
      ok = f.read(b, 4) == 4;
      FixRange &r = c.ranges[c.nRanges++];
      r.from = static_cast<uint16_t>(b[0] | (b[1] << 8));
      r.to = static_cast<uint16_t>(b[2] | (b[3] << 8));
      ok = ok && r.from <= r.to && r.to < kLedCountMax;
    }
  }
  f.close();
  if (ok) {
    s_cfg = c;
  } else {
    LOG_C("fix", "%s unreadable; fixture off", kCfgPath);
  }
  free(tmp);
}

// ---------------------------------------------------------------- names

static constexpr size_t kNameSlot = kFixNameLen + 1;

static char *loadNames(uint16_t &count) {
  const size_t bytes = static_cast<size_t>(kLedCountMax) * kNameSlot;
  char *buf = static_cast<char *>(psramFound() ? ps_malloc(bytes) : malloc(bytes));
  count = 0;
  if (!buf) {
    return nullptr;
  }
  memset(buf, 0, bytes);
  if (!s_fsOk || !LittleFS.exists(kNamesPath)) {
    return buf;
  }
  File f = LittleFS.open(kNamesPath, "r");
  if (!f) {
    return buf;
  }
  while (f.available() && count < kLedCountMax) {
    String line = f.readStringUntil('\n');
    line.trim();
    snprintf(buf + static_cast<size_t>(count) * kNameSlot, kNameSlot, "%s", line.c_str());
    ++count;
  }
  f.close();
  return buf;
}

// ---------------------------------------------------------------- clip select

static void serviceClip() {
  if (!s_cfg.en || !s_valid) {
    return;
  }
  const uint8_t *d = LiveInput::slotData(fixSource(), s_cfg.uni);
  if (!d) {
    return;
  }
  const HdrMap &m = hdrOf(s_cfg.mode);
  const uint8_t *h = d + (s_cfg.ch - 1);
  const uint8_t folder = h[m.folder];
  const uint8_t clip = h[m.clip];
  const uint16_t pick = static_cast<uint16_t>((folder << 8) | clip);
  const uint32_t now = millis();
  if (pick != s_pickSeen) {
    s_pickSeen = pick;
    s_pickSince = now;
    return;
  }
  if (now - s_pickSince < kClipSettleMs || pick == s_pickActed) {
    return;
  }
  const uint8_t was = static_cast<uint8_t>(s_pickActed & 0xFF);
  s_pickActed = pick;
  if (clip == 0) {
    // Back to 0 after a pick: the node's own startup playlist again, for this
    // session only (NVS keeps the startup show as it was).
    if (was != 0 && PlayCfg::set(PlayCfg::bootSrc(), PlayCfg::bootPath(),
                                 PlayCfg::bootFileLoop(), PlayCfg::bootFolderRep(),
                                 PlayCfg::bootFolderN(), false, false)) {
      Playback::reload();
      Sync::noteLocalTrigger();
      LOG_V("fix", "clip 0 -> startup playlist");
    }
    return;
  }
  if (!SdInfo::ok()) {
    return;
  }
  // Folder 0 = the SD root; n = the n-th folder there. Clip n = the n-th
  // look in that folder. Both A-Z, read from the card (no list limit).
  char dir[kSdPathLen] = "/";
  if (folder > 0 && !SdInfo::nthEntry("/", true, folder, dir, sizeof(dir))) {
    return;
  }
  char path[kSdPathLen];
  if (!SdInfo::nthEntry(dir, false, clip, path, sizeof(path)) ||
      !PlayCfg::set(PlaySrc::File, path, PlayFileLoop::One, PlayCfg::folderRep(),
                    PlayCfg::folderN(), false, false)) {
    return;
  }
  Playback::reload();
  Sync::noteLocalTrigger();
  LOG_V("fix", "folder %u clip %u -> %s", folder, clip, path);
}

} // namespace

// ================================================================== setup

void Fixture::begin() {
  s_fsOk = LittleFS.begin(true, "/cfg", 4, "spiffs");
  if (!s_fsOk) {
    LOG_C("fix", "no config partition; advanced patch unavailable");
  }
  loadCfg();
  rebuild();
  LOG_V("fix", "%s mode=%s %s %u.%u subs=%u fp=%u", s_cfg.en ? "on" : "off",
        modeName(s_cfg.mode), s_cfg.sacn ? "sacn" : "artnet", s_cfg.uni, s_cfg.ch,
        s_cfg.nSubs, s_footprint);
}

void Fixture::service() {
  if (PixelMap::generation() != s_mapGen) {
    const bool was = s_cfg.en && s_valid;
    rebuild();
    if (was != (s_cfg.en && s_valid)) {
      LiveInput::applyCfg();
    }
  }
  serviceClip();
}

// ================================================================== intake

bool Fixture::isFixtureUniverse(bool sacn, uint16_t uni) {
  return s_cfg.en && s_valid && sacn == s_cfg.sacn && uni >= s_cfg.uni &&
         uni < static_cast<uint32_t>(s_cfg.uni) + s_unis;
}

bool Fixture::controlOnly() { return s_cfg.en && s_valid && lookMode(s_cfg.mode); }

bool Fixture::wantsArtNet(uint16_t uni) {
  return PixelMap::wantsArtNet(uni) || isFixtureUniverse(false, uni);
}

bool Fixture::wantsSacn(uint16_t uni) {
  return PixelMap::wantsSacn(uni) || isFixtureUniverse(true, uni);
}

bool Fixture::anyArtNet() { return PixelMap::anyArtNet() || (s_cfg.en && s_valid && !s_cfg.sacn); }

bool Fixture::anySacn() { return PixelMap::anySacn() || (s_cfg.en && s_valid && s_cfg.sacn); }

uint8_t Fixture::collectSacnUniverses(uint16_t *out, uint8_t max) {
  uint8_t n = PixelMap::collectSacnUniverses(out, max);
  if (!(s_cfg.en && s_valid && s_cfg.sacn)) {
    return n;
  }
  for (uint8_t u = 0; u < s_unis && n < max; ++u) {
    const uint16_t uni = static_cast<uint16_t>(s_cfg.uni + u);
    bool have = false;
    for (uint8_t i = 0; i < n; ++i) {
      have = have || out[i] == uni;
    }
    if (!have) {
      out[n++] = uni;
    }
  }
  return n;
}

// ================================================================== render

bool Fixture::animating() {
  return s_cfg.en && s_valid && LiveInput::slotData(fixSource(), s_cfg.uni) != nullptr;
}

FixDrive Fixture::beginFrame(uint32_t nowMs) {
  (void)nowMs;
  s_drive = FixDrive::None;
  if (!s_cfg.en || !s_valid) {
    return s_drive;
  }
  for (uint8_t u = 0; u < s_unis; ++u) {
    s_data[u] = LiveInput::slotData(fixSource(), static_cast<uint16_t>(s_cfg.uni + u));
  }
  const uint8_t *h = s_data[0] ? s_data[0] + (s_cfg.ch - 1) : nullptr;
  if (lookMode(s_cfg.mode)) {
    if (!h) {
      resetDimLatches();
      return s_drive; // no console: the look plays as recorded
    }
    s_drive = FixDrive::Look;
  } else {
    s_drive = FixDrive::Console;
  }
  if (!h) {
    resetDimLatches();
    s_dim = 255;
    s_open = true;
    s_strobeInt = 0;
    s_hueOn = s_filterOn = s_addOn = false;
    memset(s_subLevel, 0, sizeof(s_subLevel));
    memset(s_subRgb, 0, sizeof(s_subRgb));
    memset(s_subOpen, 0xFF, sizeof(s_subOpen));
    return s_drive;
  }
  const HdrMap &m = hdrOf(s_cfg.mode);
  const int64_t t = SyncNet::masterUs();
  s_dim = latchDim(h[m.dim], s_masterUsed);
  s_open = strobeOn(h[m.strobe], t);
  // The strobe's off phase shows this colour at this level (0 = blackout).
  wheel(m.scol >= 0 ? h[m.scol] : 0, s_strobeRgb);
  s_strobeWhite = m.scol < 0 || h[m.scol] == 0;
  s_strobeInt = m.sint >= 0 ? h[m.sint] : 0;
  s_hueOn = h[m.hue] != 0;
  if (s_hueOn) {
    buildHue(h[m.hue]);
  }
  // Filter = how much of each colour to remove (0 = none).
  s_filterOn = m.filter >= 0 && (h[m.filter] || h[m.filter + 1] || h[m.filter + 2]);
  for (uint8_t i = 0; i < 3; ++i) {
    s_filter[i] = m.filter >= 0 ? static_cast<uint8_t>(255 - h[m.filter + i]) : 255;
  }
  s_addOn = m.add >= 0 && (h[m.add] || h[m.add + 1] || h[m.add + 2]);
  if (s_addOn) {
    memcpy(s_add, h + m.add, 3);
  }
  const uint8_t per = perSub(s_cfg.mode);
  memset(s_subOpen, 0, sizeof(s_subOpen));
  for (uint8_t k = 0; per && k < s_cfg.nSubs; ++k) {
    const uint8_t *sp = h + m.len + k * per;
    s_subLevel[k] = fxMul(latchSubDim(k, sp[0]), s_dim);
    if (s_open && strobeOn(sp[1], t)) {
      s_subOpen[k >> 3] |= static_cast<uint8_t>(1u << (k & 7));
    }
    if (s_cfg.mode == FixMode::Rgb) {
      memcpy(s_subRgb[k], sp + 2, 3);
    }
  }
  return s_drive;
}

void Fixture::applyLook(uint16_t g, uint8_t *px, uint8_t cpp) {
  if (g >= s_total) {
    return;
  }
  const uint8_t k = perSub(s_cfg.mode) ? s_subOf[g] : kNoSub;
  const uint8_t level = k == kNoSub ? s_dim : s_subLevel[k];
  if (!(k == kNoSub ? s_open : subOpen(k))) {
    strobePixel(px, cpp, level);
    return;
  }
  if (level == 255 && !s_hueOn && !s_filterOn && !s_addOn) {
    return;
  }
  fxColor(px, cpp, level);
}

void Fixture::consolePixel(uint16_t g, uint8_t *px, uint8_t cpp) {
  memset(px, 0, cpp);
  if (g >= s_total) {
    return;
  }
  if (s_cfg.mode == FixMode::Rgb) {
    const uint8_t k = s_subOf[g];
    if (k == kNoSub || cpp < 3) {
      return;
    }
    if (!subOpen(k)) {
      strobePixel(px, cpp, s_subLevel[k]);
      return;
    }
    memcpy(px, s_subRgb[k], 3);
    fxColor(px, cpp, s_subLevel[k]);
    return;
  }
  if (!s_open) {
    strobePixel(px, cpp, s_dim);
    return;
  }
  const uint16_t at = s_fullAddr[g];
  for (uint8_t i = 0; i < cpp; ++i) {
    const uint16_t a = static_cast<uint16_t>(at + i);
    const uint8_t *d = s_data[a / kDmxUniverseSize];
    px[i] = d ? d[a % kDmxUniverseSize] : 0;
  }
  fxColor(px, cpp, s_dim);
}

// ================================================================== config

bool Fixture::parseMode(const char *s, FixMode &out) {
  if (!s) {
    return false;
  }
  if (strcmp(s, "dim") == 0) {
    out = FixMode::Dim;
  } else if (strcmp(s, "rgb") == 0) {
    out = FixMode::Rgb;
  } else if (strcmp(s, "full") == 0) {
    out = FixMode::Full;
  } else if (strcmp(s, "basic") == 0) {
    out = FixMode::Basic;
  } else {
    return false;
  }
  return true;
}

const char *Fixture::modeName(FixMode mode) {
  switch (mode) {
  case FixMode::Rgb:
    return "rgb";
  case FixMode::Full:
    return "full";
  case FixMode::Basic:
    return "basic";
  case FixMode::Dim:
  default:
    return "dim";
  }
}

void Fixture::stageBegin(bool en, FixMode mode, bool sacn, uint16_t uni, uint16_t ch) {
  if (!s_stage) {
    s_stage = static_cast<Stage *>(malloc(sizeof(Stage)));
  }
  if (!s_stage) {
    return;
  }
  memset(s_stage, 0, sizeof(Stage));
  Cfg &c = s_stage->cfg;
  c.en = en;
  c.mode = mode;
  c.sacn = sacn;
  c.uni = uni;
  c.ch = ch;
}

// ranges: "0-11,24-35,40" (global pixel indices, inclusive).
bool Fixture::stageSub(const char *name, const char *ranges, const char *&error) {
  error = nullptr;
  if (!s_stage) {
    error = "no memory";
    return false;
  }
  Cfg &c = s_stage->cfg;
  if (c.nSubs >= kFixMaxSubs) {
    error = "too many sub-fixtures (96 max)";
    return false;
  }
  SubCfg &sub = c.subs[c.nSubs];
  snprintf(sub.name, sizeof(sub.name), "%s", name ? name : "");
  if (!sub.name[0]) {
    snprintf(sub.name, sizeof(sub.name), "Sub %u", static_cast<unsigned>(c.nSubs + 1));
  }
  sub.firstRange = c.nRanges;
  sub.nRanges = 0;
  const char *p = ranges ? ranges : "";
  while (*p) {
    while (*p == ' ' || *p == ',') {
      ++p;
    }
    if (!*p) {
      break;
    }
    char *end = nullptr;
    const long a = strtol(p, &end, 10);
    if (end == p) {
      error = "bad pixel range";
      return false;
    }
    long b = a;
    p = end;
    if (*p == '-') {
      ++p;
      b = strtol(p, &end, 10);
      if (end == p) {
        error = "bad pixel range";
        return false;
      }
      p = end;
    }
    if (a < 0 || b < a || b >= kLedCountMax) {
      error = "pixel range out of bounds";
      return false;
    }
    if (c.nRanges >= kFixMaxRanges || sub.nRanges == 255) {
      error = "too many pixel ranges";
      return false;
    }
    for (long g = a; g <= b; ++g) {
      uint8_t &byte = s_stage->used[g >> 3];
      const uint8_t bit = static_cast<uint8_t>(1u << (g & 7));
      if (byte & bit) {
        error = "a pixel is in two sub-fixtures";
        return false;
      }
      byte |= bit;
    }
    c.ranges[c.nRanges++] = {static_cast<uint16_t>(a), static_cast<uint16_t>(b)};
    ++sub.nRanges;
  }
  ++c.nSubs;
  return true;
}

bool Fixture::stageCommit(const char *&error) {
  error = nullptr;
  if (!s_stage) {
    error = "no memory";
    return false;
  }
  uint16_t footprint = 0;
  uint8_t unis = 1;
  const char *err = "";
  if (!derive(s_stage->cfg, false, footprint, unis, err)) {
    error = err;
    return false;
  }
  if (!s_fsOk) {
    error = "no config partition (flash the node once over USB)";
    return false;
  }
  if (!saveCfg(s_stage->cfg)) {
    error = "save failed";
    return false;
  }
  s_cfg = s_stage->cfg;
  free(s_stage);
  s_stage = nullptr;
  rebuild();
  s_pickActed = 0;
  s_pickSeen = 0;
  LiveInput::applyCfg();
  LOG_V("fix", "saved %s mode=%s subs=%u fp=%u unis=%u", s_cfg.en ? "on" : "off",
        modeName(s_cfg.mode), s_cfg.nSubs, s_footprint, s_unis);
  return true;
}

// ================================================================== locate

bool Fixture::locate(const char *ranges, uint32_t ms, const char *&error) {
  error = nullptr;
  if (ms == 0) {
    cancelLocate();
    return true;
  }
  if (ms > 15000) {
    ms = 15000;
  }
  memset(s_locate, 0, sizeof(s_locate));
  const char *p = ranges ? ranges : "";
  while (*p) {
    while (*p == ' ' || *p == ',') {
      ++p;
    }
    if (!*p) {
      break;
    }
    char *end = nullptr;
    const long a = strtol(p, &end, 10);
    if (end == p) {
      error = "bad pixel range";
      return false;
    }
    long b = a;
    p = end;
    if (*p == '-') {
      ++p;
      b = strtol(p, &end, 10);
      if (end == p) {
        error = "bad pixel range";
        return false;
      }
      p = end;
    }
    if (a < 0 || b < a || b >= kLedCountMax) {
      error = "pixel range out of bounds";
      return false;
    }
    for (long g = a; g <= b; ++g) {
      s_locate[g >> 3] |= static_cast<uint8_t>(1u << (g & 7));
    }
  }
  s_locateUntil = millis() + ms;
  s_locateOn = true;
  return true;
}

bool Fixture::locating() {
  if (s_locateOn && static_cast<int32_t>(millis() - s_locateUntil) >= 0) {
    s_locateOn = false;
  }
  return s_locateOn;
}

void Fixture::cancelLocate() { s_locateOn = false; }

void Fixture::renderLocate() {
  LedBus::clear();
  const uint16_t total = PixelMap::totalPixels();
  for (uint16_t g = 0; g < total; ++g) {
    if (s_locate[g >> 3] & (1u << (g & 7))) {
      LedBus::setRgb(g, 255, 255, 255);
    }
  }
}

// ================================================================== JSON

void Fixture::appendJson(String &out) {
  out += "{\"en\":";
  out += s_cfg.en ? "true" : "false";
  out += ",\"mode\":\"";
  out += modeName(s_cfg.mode);
  out += "\",\"proto\":\"";
  out += s_cfg.sacn ? "sacn" : "artnet";
  out += "\",\"uni\":";
  out += static_cast<unsigned>(s_cfg.uni);
  out += ",\"ch\":";
  out += static_cast<unsigned>(s_cfg.ch);
  out += ",\"hdr\":";
  out += static_cast<unsigned>(hdrOf(s_cfg.mode).len);
  out += ",\"max_subs\":";
  out += static_cast<unsigned>(kFixMaxSubs);
  out += ",\"pixels\":";
  out += static_cast<unsigned>(s_total);
  out += ",\"footprint\":";
  out += static_cast<unsigned>(s_footprint);
  out += ",\"unis\":";
  out += static_cast<unsigned>(s_unis);
  out += ",\"valid\":";
  out += s_valid ? "true" : "false";
  out += ",\"err\":";
  appendEsc(out, s_err);
  out += ",\"storage\":";
  out += s_fsOk ? "true" : "false";
  out += ",\"subs\":[";
  for (uint8_t k = 0; k < s_cfg.nSubs; ++k) {
    if (k) {
      out += ',';
    }
    out += "{\"name\":";
    appendEsc(out, s_cfg.subs[k].name);
    out += ",\"px\":";
    appendRanges(out, s_cfg, s_cfg.subs[k]);
    out += '}';
  }
  out += "]}";
}

void Fixture::appendStatus(String &out) {
  out += "\"fixture\":{\"en\":";
  out += s_cfg.en ? "true" : "false";
  out += ",\"mode\":\"";
  out += modeName(s_cfg.mode);
  out += "\",\"proto\":\"";
  out += s_cfg.sacn ? "sacn" : "artnet";
  out += "\",\"uni\":";
  out += static_cast<unsigned>(s_cfg.uni);
  out += ",\"ch\":";
  out += static_cast<unsigned>(s_cfg.ch);
  out += ",\"footprint\":";
  out += static_cast<unsigned>(s_footprint);
  out += ",\"unis\":";
  out += static_cast<unsigned>(s_unis);
  out += ",\"subs\":";
  out += static_cast<unsigned>(s_cfg.nSubs);
  out += ",\"valid\":";
  out += s_valid ? "true" : "false";
  out += ",\"err\":";
  appendEsc(out, s_err);
  out += ",\"ctl\":";
  out += s_cfg.en && s_valid && LiveInput::slotData(fixSource(), s_cfg.uni) ? "true" : "false";
  out += ",\"hdr\":";
  out += static_cast<unsigned>(hdrOf(s_cfg.mode).len);
  out += ",\"folder\":";
  out += static_cast<unsigned>(s_pickActed >> 8);
  out += ",\"clip\":";
  out += static_cast<unsigned>(s_pickActed & 0xFF);
  out += '}';
}

void Fixture::appendNames(String &out, uint16_t from, uint16_t n) {
  uint16_t count = 0;
  char *buf = loadNames(count);
  out += "{\"from\":";
  out += static_cast<unsigned>(from);
  out += ",\"total\":";
  out += static_cast<unsigned>(s_total);
  out += ",\"names\":[";
  for (uint16_t i = 0; i < n && from + i < s_total; ++i) {
    if (i) {
      out += ',';
    }
    const uint16_t g = static_cast<uint16_t>(from + i);
    appendEsc(out, buf && g < count ? buf + static_cast<size_t>(g) * kNameSlot : "");
  }
  out += "]}";
  free(buf);
}

bool Fixture::setNames(uint16_t from, const String &lines, const char *&error) {
  error = nullptr;
  if (!s_fsOk) {
    error = "no config partition (flash the node once over USB)";
    return false;
  }
  uint16_t count = 0;
  char *buf = loadNames(count);
  if (!buf) {
    error = "no memory";
    return false;
  }
  uint16_t g = from;
  int start = 0;
  const int len = static_cast<int>(lines.length());
  while (start <= len && g < kLedCountMax) {
    int nl = lines.indexOf('\n', start);
    if (nl < 0) {
      nl = len;
    }
    String name = lines.substring(start, nl);
    name.trim();
    snprintf(buf + static_cast<size_t>(g) * kNameSlot, kNameSlot, "%s", name.c_str());
    ++g;
    start = nl + 1;
  }
  if (g > count) {
    count = g;
  }
  while (count > 0 && buf[static_cast<size_t>(count - 1) * kNameSlot] == '\0') {
    --count;
  }
  File f = LittleFS.open(kNamesPath, "w");
  bool ok = static_cast<bool>(f);
  for (uint16_t i = 0; ok && i < count; ++i) {
    const char *name = buf + static_cast<size_t>(i) * kNameSlot;
    ok = f.print(name) >= 0 && f.print('\n') == 1;
  }
  if (f) {
    f.close();
  }
  free(buf);
  if (!ok) {
    error = "save failed";
  }
  return ok;
}
