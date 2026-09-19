#include "led_bus.h"

#include "log.h"
#include "pixel_map.h"

#include <Arduino.h>
#include <FastLED.h>

#include "platforms/esp/32/clockless_rmt_esp32.h"

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
static bool s_begun = false;
static bool s_applyPending = false;
static int s_boundPin = -1;
static LedWire s_boundWire = LedWire::Clockless;

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
    s_rmt = new RmtController(pin, t1, t2, t3, FASTLED_RMT_MAX_CHANNELS,
                              FASTLED_RMT_BUILTIN_DRIVER);
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
    s_rmt->showPixels(iterator);
  }

private:
  RmtController *s_rmt = nullptr;
};

static RuntimeClockless s_ctrl;

static void timings(int &t1, int &t2, int &t3) {
  uint8_t a = 2;
  uint8_t b = 5;
  uint8_t c = 3;
  PixelMap::clocklessUnits(a, b, c);
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

static void packPixel(uint16_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                      uint8_t c) {
  const PixelMapCfg &m = PixelMap::cfg();
  const uint8_t n = m.channelsPerPixel;
  const char *ord = m.colorOrder;
  for (uint8_t k = 0; k < kMaxChannelsPerPixel; ++k) {
    s_px[i][k] = 0;
  }
  for (uint8_t k = 0; k < n && ord[k] != '\0'; ++k) {
    s_px[i][k] = chanOf(ord[k], r, g, b, w, c);
  }
  if (n == 3) {
    s_leds[i] = CRGB(s_px[i][0], s_px[i][1], s_px[i][2]);
  }
}

static uint8_t scaled(uint8_t v) {
  return static_cast<uint8_t>((static_cast<uint16_t>(v) * FastLED.getBrightness()) /
                              255u);
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

static void showClocked() {
  const PixelMapCfg &m = PixelMap::cfg();
  const uint8_t data = m.dataGpio;
  const uint8_t clk = m.clockGpio;
  const uint16_t n = m.pixelCount;
  const LedWire wire = PixelMap::wireKind();
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
      writeByteMsb(data, clk, scaled(s_px[p][0]));
      writeByteMsb(data, clk, scaled(s_px[p][1]));
      writeByteMsb(data, clk, scaled(s_px[p][2]));
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
      const uint8_t r = scaled(s_px[p][0]);
      const uint8_t g = scaled(s_px[p][1]);
      const uint8_t b = scaled(s_px[p][2]);
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
      writeByteMsb(data, clk, static_cast<uint8_t>(0x80 | (scaled(s_px[p][1]) >> 1)));
      writeByteMsb(data, clk, static_cast<uint8_t>(0x80 | (scaled(s_px[p][0]) >> 1)));
      writeByteMsb(data, clk, static_cast<uint8_t>(0x80 | (scaled(s_px[p][2]) >> 1)));
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
      const uint16_t r = scaled(s_px[p][0]) >> 3;
      const uint16_t g = scaled(s_px[p][1]) >> 3;
      const uint16_t b = scaled(s_px[p][2]) >> 3;
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
    const uint8_t ch = m.channelsPerPixel;
    for (uint8_t k = 0; k < ch; ++k) {
      writeByteMsb(data, clk, scaled(s_px[p][k]));
    }
  }
  for (uint8_t i = 0; i < 16; ++i) {
    writeBit(data, clk, false);
  }
}

static void showClockless() {
  const PixelMapCfg &m = PixelMap::cfg();
  const uint16_t n = m.pixelCount;
  const uint8_t ch = m.channelsPerPixel;
  if (ch <= 3) {
    s_ctrl.setLeds(s_leds, static_cast<int>(n));
    FastLED.show();
    return;
  }
  const uint32_t bytes = static_cast<uint32_t>(n) * ch;
  const uint16_t fake =
      static_cast<uint16_t>((bytes + 2u) / 3u);
  uint8_t *raw = reinterpret_cast<uint8_t *>(s_leds);
  const uint32_t cap = static_cast<uint32_t>(kLedCountMax) * 3u;
  memset(raw, 0, cap);
  uint32_t o = 0;
  for (uint16_t p = 0; p < n && o + ch <= cap; ++p) {
    memcpy(raw + o, s_px[p], ch);
    o += ch;
  }
  s_ctrl.setLeds(s_leds, static_cast<int>(fake > kLedCountMax ? kLedCountMax : fake));
  FastLED.show();
  s_ctrl.setLeds(s_leds, static_cast<int>(n));
  for (uint16_t p = 0; p < n; ++p) {
    if (ch >= 3) {
      s_leds[p] = CRGB(s_px[p][0], s_px[p][1], s_px[p][2]);
    }
  }
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
  const PixelMapCfg &m = PixelMap::cfg();
  const LedWire wire = PixelMap::wireKind();
  const int pin = static_cast<int>(m.dataGpio);
  const bool clocked = wire != LedWire::Clockless;

  if (clocked) {
    if (s_boundWire == LedWire::Clockless && s_ctrl.bound()) {
      s_ctrl.release();
      s_boundPin = -1;
      LOG_V("led", "rmt released (clocked)");
    }
    s_boundWire = wire;
    pinMode(m.dataGpio, OUTPUT);
    pinMode(m.clockGpio, OUTPUT);
    LOG_V("led", "bus clocked pin=%u clk=%u count=%u chip=%s order=%s ch=%u",
          m.dataGpio, m.clockGpio, m.pixelCount, PixelMap::chipsetName(),
          PixelMap::colorOrderName(), m.channelsPerPixel);
    return;
  }

  int t1 = 0;
  int t2 = 0;
  int t3 = 0;
  timings(t1, t2, t3);

  if (s_ctrl.bound() && s_boundPin == pin && s_boundWire == LedWire::Clockless) {
    s_ctrl.setLeds(s_leds, static_cast<int>(m.pixelCount));
    for (uint16_t i = m.pixelCount; i < kLedCountMax; ++i) {
      s_leds[i] = CRGB::Black;
    }
    LOG_V("led", "bus pin=%u count=%u chip=%s order=%s (timings deferred)",
          m.dataGpio, m.pixelCount, PixelMap::chipsetName(),
          PixelMap::colorOrderName());
    return;
  }

  s_ctrl.rebind(pin, t1, t2, t3);
  if (!s_ctrl.bound()) {
    LOG_C("led", "rmt rebind failed pin=%u", m.dataGpio);
    return;
  }
  s_boundPin = pin;
  s_boundWire = LedWire::Clockless;
  s_ctrl.setLeds(s_leds, static_cast<int>(m.pixelCount));
  for (uint16_t i = m.pixelCount; i < kLedCountMax; ++i) {
    s_leds[i] = CRGB::Black;
  }
  LOG_V("led", "rebind pin=%u count=%u chip=%s order=%s ch=%u", m.dataGpio,
        m.pixelCount, PixelMap::chipsetName(), PixelMap::colorOrderName(),
        m.channelsPerPixel);
}

void LedBus::begin() {
  if (s_begun) {
    apply();
    return;
  }
  s_begun = true;
  apply();
  FastLED.addLeds(&s_ctrl, s_leds, static_cast<int>(PixelMap::cfg().pixelCount));
}

void LedBus::show() {
  service();
  if (PixelMap::wireKind() != LedWire::Clockless) {
    showClocked();
    return;
  }
  if (!s_ctrl.bound()) {
    return;
  }
  showClockless();
}

CRGB *LedBus::leds() { return s_leds; }

uint16_t LedBus::count() { return PixelMap::cfg().pixelCount; }

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
  const PixelMapCfg &m = PixelMap::cfg();
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

void LedBus::fillRgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t n = count();
  for (uint16_t i = 0; i < n; ++i) {
    packPixel(i, r, g, b, 0, 0);
  }
}

void LedBus::clear() { fillRgb(0, 0, 0); }
