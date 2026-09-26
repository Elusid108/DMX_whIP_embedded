#include "led_test.h"

#include "led_bus.h"
#include "log.h"
#include "pixel_map.h"

namespace {

static uint8_t s_mode[kPatchMaxOutputs] = {};

static void trim() {
  const uint8_t n = PixelMap::outputCount();
  for (uint8_t i = n; i < kPatchMaxOutputs; ++i) {
    s_mode[i] = static_cast<uint8_t>(LedTestMode::Off);
  }
}

static const char *nameOf(LedTestMode mode) {
  switch (mode) {
  case LedTestMode::Rainbow:
    return "rainbow";
  case LedTestMode::Cycle:
    return "cycle";
  case LedTestMode::Ends:
    return "ends";
  case LedTestMode::Off:
  default:
    return "off";
  }
}

static void hueRgb(uint8_t hue, uint8_t &r, uint8_t &g, uint8_t &b) {
  const uint8_t region = static_cast<uint8_t>(hue / 43);
  const uint8_t rem = static_cast<uint8_t>((hue - region * 43) * 6);
  const uint8_t q = static_cast<uint8_t>(255 - rem);
  switch (region) {
  case 0:
    r = 255;
    g = rem;
    b = 0;
    break;
  case 1:
    r = q;
    g = 255;
    b = 0;
    break;
  case 2:
    r = 0;
    g = 255;
    b = rem;
    break;
  case 3:
    r = 0;
    g = q;
    b = 255;
    break;
  case 4:
    r = rem;
    g = 0;
    b = 255;
    break;
  default:
    r = 255;
    g = 0;
    b = q;
    break;
  }
}

static void paintSpan(uint16_t off, uint16_t n, uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t p = 0; p < n; ++p) {
    LedBus::setRgb(static_cast<uint16_t>(off + p), r, g, b);
  }
}

} // namespace

bool LedTest::set(uint8_t out, LedTestMode mode) {
  if (out >= kPatchMaxOutputs || out >= PixelMap::outputCount()) {
    return false;
  }
  s_mode[out] = static_cast<uint8_t>(mode);
  LOG_V("test", "out=%u %s", static_cast<unsigned>(out), nameOf(mode));
  return true;
}

void LedTest::cancel() {
  bool any = false;
  for (uint8_t i = 0; i < kPatchMaxOutputs; ++i) {
    if (s_mode[i] != static_cast<uint8_t>(LedTestMode::Off)) {
      any = true;
    }
    s_mode[i] = static_cast<uint8_t>(LedTestMode::Off);
  }
  if (any) {
    LOG_V("test", "cancel");
  }
}

bool LedTest::active() {
  trim();
  const uint8_t n = PixelMap::outputCount();
  const uint8_t lim = n < kPatchMaxOutputs ? n : kPatchMaxOutputs;
  for (uint8_t i = 0; i < lim; ++i) {
    if (s_mode[i] != static_cast<uint8_t>(LedTestMode::Off)) {
      return true;
    }
  }
  return false;
}

LedTestMode LedTest::mode(uint8_t out) {
  if (out >= kPatchMaxOutputs || out >= PixelMap::outputCount()) {
    return LedTestMode::Off;
  }
  return static_cast<LedTestMode>(s_mode[out]);
}

const char *LedTest::modeName(uint8_t out) { return nameOf(mode(out)); }

void LedTest::render(uint32_t now) {
  trim();
  LedBus::clear();
  const uint8_t nOut = PixelMap::outputCount();
  const uint8_t lim = nOut < kPatchMaxOutputs ? nOut : kPatchMaxOutputs;
  for (uint8_t o = 0; o < lim; ++o) {
    const auto mode = static_cast<LedTestMode>(s_mode[o]);
    if (mode == LedTestMode::Off) {
      continue;
    }
    const uint16_t n = PixelMap::outputPixelCount(o);
    const uint16_t off = PixelMap::outputPixelOffset(o);
    if (n == 0) {
      continue;
    }
    if (mode == LedTestMode::Rainbow) {
      const uint8_t crawl = static_cast<uint8_t>((now / 20) & 0xFF);
      for (uint16_t p = 0; p < n; ++p) {
        const uint8_t hue =
            static_cast<uint8_t>(crawl + static_cast<uint16_t>((p * 256u) / n));
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        hueRgb(hue, r, g, b);
        LedBus::setRgb(static_cast<uint16_t>(off + p), r, g, b);
      }
    } else if (mode == LedTestMode::Cycle) {
      static const uint8_t kCycle[7][3] = {
          {255, 0, 0},   {0, 255, 0},   {0, 0, 255},  {0, 255, 255},
          {255, 0, 255}, {255, 255, 0}, {255, 255, 255},
      };
      const uint8_t step = static_cast<uint8_t>((now / 600) % 7);
      paintSpan(off, n, kCycle[step][0], kCycle[step][1], kCycle[step][2]);
    } else if (mode == LedTestMode::Ends) {
      LedBus::setRgb(off, 50, 205, 50);
      if (n > 1) {
        LedBus::setRgb(static_cast<uint16_t>(off + n - 1), 255, 0, 255);
      }
    }
  }
}
