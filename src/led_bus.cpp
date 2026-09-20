#include "led_bus.h"

#include "identify.h"
#include "log.h"
#include "pixel_map.h"

#include <Arduino.h>
#include <FastLED.h>

#include "platforms/esp/32/rmt_5/idf5_rmt.h"

#ifndef CLOCKLESS_FREQUENCY
#define CLOCKLESS_FREQUENCY F_CPU
#endif

namespace {

#ifndef FMUL
#define LEDBUS_FMUL (CLOCKLESS_FREQUENCY / 8000000)
#else
#define LEDBUS_FMUL FMUL
#endif

static CRGB s_leds[kLedCountMax];
static uint8_t s_px[kLedCountMax][kMaxChannelsPerPixel];
static uint8_t s_ch[kLedCountMax];
static bool s_begun = false;
static bool s_applyPending = false;

class RuntimeClockless : public CPixelLEDController<RGB> {
public:
  RuntimeClockless() = default;
  ~RuntimeClockless() { release(); }

  void release() {
    delete s_rmt;
    s_rmt = nullptr;
  }

  void rebind(int pin, int t1, int t2, int t3) {
    release();
    s_rmt = new fl::RmtController5(pin, t1, t2, t3,
                                   fl::RmtController5::DMA_AUTO);
  }

  bool bound() const { return s_rmt != nullptr; }

  void init() override {}

  fl::u16 getMaxRefreshRate() const override { return 400; }

protected:
  void showPixels(PixelController<RGB> &pixels) override {
    if (s_rmt == nullptr) {
      return;
    }
    PixelIterator iterator = pixels.as_iterator(this->getRgbw());
    s_rmt->loadPixelData(iterator);
    s_rmt->showPixels();
  }

private:
  fl::RmtController5 *s_rmt = nullptr;
};

static RuntimeClockless s_ctrl[kPatchMaxOutputs];
static int s_boundPin[kPatchMaxOutputs];
static LedWire s_boundWire[kPatchMaxOutputs];

static void timings(LedChipset chip, int &t1, int &t2, int &t3) {
  uint8_t a = 2;
  uint8_t b = 5;
  uint8_t c = 3;
  PixelMap::clocklessUnits(chip, a, b, c);
  const int fmul = LEDBUS_FMUL;
  t1 = a * fmul;
  t2 = b * fmul;
  t3 = c * fmul;
}

static uint8_t chanOf(char letter, uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                      uint8_t c) {
  switch (letter) {
  case 'r':
    return r;
  case 'g':
    return g;
  case 'b':
    return b;
  case 'w':
    return w;
  case 'c':
    return c;
  default:
    return 0;
  }
}

static uint8_t scaleSeg(uint8_t v, uint8_t bri) {
  return static_cast<uint8_t>((static_cast<uint16_t>(v) * bri) / 255u);
}

static uint8_t onceBri(uint8_t segBri) {
  if (Identify::active()) {
    return FastLED.getBrightness();
  }
  return segBri;
}

static void packPixel(uint16_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                      uint8_t c) {
  uint8_t seg = 0;
  uint16_t local = 0;
  if (!PixelMap::locatePixel(i, seg, local)) {
    return;
  }
  const PixelMapCfg &m = PixelMap::segment(seg);
  const uint8_t n = m.channelsPerPixel;
  const char *ord = m.colorOrder;
  const uint8_t bri = onceBri(m.brightness);
  r = scaleSeg(r, bri);
  g = scaleSeg(g, bri);
  b = scaleSeg(b, bri);
  w = scaleSeg(w, bri);
  c = scaleSeg(c, bri);
  for (uint8_t k = 0; k < kMaxChannelsPerPixel; ++k) {
    s_px[i][k] = 0;
  }
  for (uint8_t k = 0; k < n && ord[k] != '\0'; ++k) {
    s_px[i][k] = chanOf(ord[k], r, g, b, w, c);
  }
  s_ch[i] = n;
  if (n >= 3) {
    s_leds[i] = CRGB(s_px[i][0], s_px[i][1], s_px[i][2]);
  }
}

static inline void clkPulse(uint8_t clk) {
  digitalWrite(clk, HIGH);
  digitalWrite(clk, LOW);
}

static void writeBit(uint8_t data, uint8_t clk, bool one) {
  digitalWrite(data, one ? HIGH : LOW);
  clkPulse(clk);
}

static void writeByteMsb(uint8_t data, uint8_t clk, uint8_t v) {
  for (int i = 7; i >= 0; --i) {
    writeBit(data, clk, (v >> i) & 1);
  }
}

static void showClocked(uint8_t out) {
  const uint8_t parent = PixelMap::firstSegmentOfOutput(out);
  const PixelMapCfg &m = PixelMap::segment(parent);
  const uint8_t data = m.dataGpio;
  const uint8_t clk = m.clockGpio;
  const uint16_t n = PixelMap::outputPixelCount(out);
  const uint16_t off = PixelMap::outputPixelOffset(out);
  const LedWire wire = PixelMap::wireKind(m.chipset);
  pinMode(data, OUTPUT);
  pinMode(clk, OUTPUT);
  digitalWrite(data, LOW);
  digitalWrite(clk, LOW);

  if (wire == LedWire::Apa102) {
    for (uint8_t i = 0; i < 32; ++i) {
      writeBit(data, clk, false);
    }
    for (uint16_t p = 0; p < n; ++p) {
      writeByteMsb(data, clk, 0xE0 | 31);
      writeByteMsb(data, clk, s_px[off + p][0]);
      writeByteMsb(data, clk, s_px[off + p][1]);
      writeByteMsb(data, clk, s_px[off + p][2]);
    }
    const uint16_t latch = static_cast<uint16_t>((n / 2) + 1);
    for (uint16_t i = 0; i < latch; ++i) {
      writeBit(data, clk, true);
    }
    return;
  }

  if (wire == LedWire::P9813) {
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    for (uint16_t p = 0; p < n; ++p) {
      const uint8_t r = s_px[off + p][0];
      const uint8_t g = s_px[off + p][1];
      const uint8_t b = s_px[off + p][2];
      const uint8_t flag = static_cast<uint8_t>(
          0xC0 | ((~b) >> 2 & 0x30) | ((~g) >> 4 & 0x0C) | ((~r) >> 6 & 0x03));
      writeByteMsb(data, clk, flag);
      writeByteMsb(data, clk, b);
      writeByteMsb(data, clk, g);
      writeByteMsb(data, clk, r);
    }
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    writeByteMsb(data, clk, 0);
    return;
  }

  if (wire == LedWire::Lpd8806) {
    for (uint8_t i = 0; i < 24; ++i) {
      writeBit(data, clk, false);
    }
    for (uint16_t p = 0; p < n; ++p) {
      writeByteMsb(data, clk,
                   static_cast<uint8_t>(0x80 | (s_px[off + p][1] >> 1)));
      writeByteMsb(data, clk,
                   static_cast<uint8_t>(0x80 | (s_px[off + p][0] >> 1)));
      writeByteMsb(data, clk,
                   static_cast<uint8_t>(0x80 | (s_px[off + p][2] >> 1)));
    }
    for (uint8_t i = 0; i < 24; ++i) {
      writeBit(data, clk, false);
    }
    return;
  }

  if (wire == LedWire::Lpd6803) {
    for (uint8_t i = 0; i < 32; ++i) {
      writeBit(data, clk, false);
    }
    for (uint16_t p = 0; p < n; ++p) {
      const uint16_t r = s_px[off + p][0] >> 3;
      const uint16_t g = s_px[off + p][1] >> 3;
      const uint16_t b = s_px[off + p][2] >> 3;
      const uint16_t v = static_cast<uint16_t>(0x8000 | (r << 10) | (g << 5) | b);
      writeByteMsb(data, clk, static_cast<uint8_t>(v >> 8));
      writeByteMsb(data, clk, static_cast<uint8_t>(v));
    }
    for (uint8_t i = 0; i < 32; ++i) {
      writeBit(data, clk, false);
    }
    return;
  }

  for (uint16_t p = 0; p < n; ++p) {
    const uint8_t ch = s_ch[off + p] ? s_ch[off + p] : m.channelsPerPixel;
    for (uint8_t k = 0; k < ch; ++k) {
      writeByteMsb(data, clk, s_px[off + p][k]);
    }
  }
  for (uint8_t i = 0; i < 16; ++i) {
    writeBit(data, clk, false);
  }
}

static bool outputWide(uint8_t out) {
  const uint8_t n = PixelMap::segmentCount();
  for (uint8_t i = 0; i < n; ++i) {
    if (PixelMap::outputOfSegment(i) == out &&
        PixelMap::segment(i).channelsPerPixel > 3) {
      return true;
    }
  }
  return false;
}

static void showClockless(uint8_t out) {
  const uint16_t n = PixelMap::outputPixelCount(out);
  const uint16_t off = PixelMap::outputPixelOffset(out);
  if (n == 0 || !s_ctrl[out].bound()) {
    return;
  }
  if (!outputWide(out)) {
    s_ctrl[out].setLeds(s_leds + off, static_cast<int>(n));
    s_ctrl[out].showLeds(255);
    return;
  }
  static uint8_t scratch[kLedCountMax * 3];
  memset(scratch, 0, sizeof(scratch));
  uint32_t o = 0;
  const uint32_t cap = sizeof(scratch);
  for (uint16_t p = 0; p < n && o < cap; ++p) {
    const uint8_t ch = s_ch[off + p] ? s_ch[off + p] : 3;
    const uint32_t take = ch;
    if (o + take > cap) {
      break;
    }
    memcpy(scratch + o, s_px[off + p], take);
    o += take;
  }
  const uint16_t fake = static_cast<uint16_t>((o + 2u) / 3u);
  memcpy(s_leds + off, scratch,
         fake * 3u > (kLedCountMax - off) * 3u ? (kLedCountMax - off) * 3u
                                               : fake * 3u);
  s_ctrl[out].setLeds(s_leds + off, static_cast<int>(fake > n ? n : fake));
  if (fake > n) {
    s_ctrl[out].setLeds(s_leds + off, static_cast<int>(fake));
  }
  s_ctrl[out].showLeds(255);
  for (uint16_t p = 0; p < n; ++p) {
    s_leds[off + p] = CRGB(s_px[off + p][0], s_px[off + p][1], s_px[off + p][2]);
  }
  s_ctrl[out].setLeds(s_leds + off, static_cast<int>(n));
}

static bool pinsUnchanged() {
  const uint8_t n = PixelMap::outputCount();
  for (uint8_t o = 0; o < n; ++o) {
    const uint8_t parent = PixelMap::firstSegmentOfOutput(o);
    const PixelMapCfg &m = PixelMap::segment(parent);
    const LedWire wire = PixelMap::wireKind(m.chipset);
    if (s_boundPin[o] != static_cast<int>(m.dataGpio) ||
        s_boundWire[o] != wire) {
      return false;
    }
    if (wire == LedWire::Clockless && !s_ctrl[o].bound()) {
      return false;
    }
  }
  for (uint8_t o = n; o < kPatchMaxOutputs; ++o) {
    if (s_boundPin[o] >= 0) {
      return false;
    }
  }
  return true;
}

} // namespace

void LedBus::requestApply() { s_applyPending = true; }

void LedBus::service() {
  if (!s_applyPending) {
    return;
  }
  s_applyPending = false;
  apply();
}

void LedBus::apply() {
  const uint8_t n = PixelMap::outputCount();
  if (pinsUnchanged()) {
    for (uint8_t o = 0; o < n; ++o) {
      const uint16_t count = PixelMap::outputPixelCount(o);
      const uint16_t off = PixelMap::outputPixelOffset(o);
      if (s_boundWire[o] == LedWire::Clockless && s_ctrl[o].bound()) {
        s_ctrl[o].setLeds(s_leds + off, static_cast<int>(count));
      }
    }
    LOG_V("led", "bus %u out (pins unchanged)", n);
    return;
  }

  for (uint8_t o = 0; o < kPatchMaxOutputs; ++o) {
    if (s_ctrl[o].bound()) {
      s_ctrl[o].release();
    }
    s_boundPin[o] = -1;
    s_boundWire[o] = LedWire::Clockless;
  }

  for (uint8_t o = 0; o < n; ++o) {
    const uint8_t parent = PixelMap::firstSegmentOfOutput(o);
    const PixelMapCfg &m = PixelMap::segment(parent);
    const LedWire wire = PixelMap::wireKind(m.chipset);
    const uint16_t count = PixelMap::outputPixelCount(o);
    const uint16_t off = PixelMap::outputPixelOffset(o);
    s_boundWire[o] = wire;
    s_boundPin[o] = static_cast<int>(m.dataGpio);
    if (wire != LedWire::Clockless) {
      pinMode(m.dataGpio, OUTPUT);
      pinMode(m.clockGpio, OUTPUT);
      LOG_V("led", "out%u clocked pin=%u clk=%u count=%u chip=%s", o,
            m.dataGpio, m.clockGpio, count, PixelMap::chipsetName(m.chipset));
      continue;
    }
    int t1 = 0;
    int t2 = 0;
    int t3 = 0;
    timings(m.chipset, t1, t2, t3);
    s_ctrl[o].rebind(static_cast<int>(m.dataGpio), t1, t2, t3);
    if (!s_ctrl[o].bound()) {
      LOG_C("led", "rmt rebind failed pin=%u", m.dataGpio);
      s_boundPin[o] = -1;
      continue;
    }
    s_ctrl[o].setLeds(s_leds + off, static_cast<int>(count));
    LOG_V("led", "out%u rebind pin=%u count=%u chip=%s", o, m.dataGpio, count,
          PixelMap::chipsetName(m.chipset));
  }
}

void LedBus::begin() {
  if (s_begun) {
    apply();
    return;
  }
  s_begun = true;
  for (uint8_t o = 0; o < kPatchMaxOutputs; ++o) {
    s_boundPin[o] = -1;
    s_boundWire[o] = LedWire::Clockless;
  }
  memset(s_ch, 3, sizeof(s_ch));
  apply();
}

void LedBus::show() {
  service();
  const uint8_t n = PixelMap::outputCount();
  for (uint8_t o = 0; o < n; ++o) {
    if (s_boundWire[o] != LedWire::Clockless) {
      showClocked(o);
    } else {
      showClockless(o);
    }
  }
}

CRGB *LedBus::leds() { return s_leds; }

uint16_t LedBus::count() { return PixelMap::totalPixels(); }

void LedBus::setRgb(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  if (i >= count()) {
    return;
  }
  packPixel(i, r, g, b, 0, 0);
}

void LedBus::setPacked(uint16_t i, const uint8_t *ch) {
  if (i >= count() || ch == nullptr) {
    return;
  }
  uint8_t seg = 0;
  uint16_t local = 0;
  if (!PixelMap::locatePixel(i, seg, local)) {
    return;
  }
  const PixelMapCfg &m = PixelMap::segment(seg);
  uint8_t r = ch[0];
  uint8_t g = ch[1];
  uint8_t b = ch[2];
  uint8_t w = 0;
  uint8_t c = 0;
  uint8_t o = 3;
  if (m.white) {
    w = ch[o++];
  }
  if (m.cct) {
    c = ch[o];
  }
  packPixel(i, r, g, b, w, c);
}

void LedBus::setOutputPacked(uint8_t out, const uint8_t *ch, uint16_t len) {
  if (ch == nullptr || out >= PixelMap::outputCount()) {
    return;
  }
  uint16_t gi = PixelMap::outputPixelOffset(out);
  uint16_t o = 0;
  const uint8_t n = PixelMap::segmentCount();
  for (uint8_t i = 0; i < n; ++i) {
    if (PixelMap::outputOfSegment(i) != out) {
      continue;
    }
    const PixelMapCfg &m = PixelMap::segment(i);
    const uint8_t pxch = m.channelsPerPixel;
    for (uint16_t p = 0; p < m.pixelCount; ++p) {
      if (static_cast<uint16_t>(o + pxch) <= len) {
        setPacked(gi, ch + o);
      } else {
        setRgb(gi, 0, 0, 0);
      }
      o = static_cast<uint16_t>(o + pxch);
      ++gi;
    }
  }
}

void LedBus::fillRgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t n = count();
  for (uint16_t i = 0; i < n; ++i) {
    packPixel(i, r, g, b, 0, 0);
  }
}

void LedBus::clear() { fillRgb(0, 0, 0); }
