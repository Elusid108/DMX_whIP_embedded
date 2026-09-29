// RP2040 / RP2350 LED bus: F4 placeholder that keeps the pixel buffer so the
// rest of the firmware runs; nothing reaches a strip yet. The PIO driver
// replaces show() in F5 (led_bus.cpp stays the ESP32 RMT / SPI version).
#if defined(ARDUINO_ARCH_RP2040)

#include "led_bus.h"

#include "log.h"
#include "pixel_map.h"

namespace {

static CRGB s_leds[kLedCountMax];
static bool s_applyPending = false;
static uint32_t s_shows = 0;

} // namespace

void LedBus::begin() {
  apply();
  LOG_V("led", "rp placeholder bus: %u px (no output until the PIO driver)",
        static_cast<unsigned>(count()));
}

void LedBus::apply() {
  s_applyPending = false;
  clear();
}

void LedBus::requestApply() { s_applyPending = true; }

void LedBus::service() {
  if (s_applyPending) {
    apply();
  }
}

void LedBus::show() { ++s_shows; }

CRGB *LedBus::leds() { return s_leds; }

uint16_t LedBus::count() {
  const uint16_t n = PixelMap::totalPixels();
  return n > kLedCountMax ? kLedCountMax : n;
}

void LedBus::setRgb(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  if (i < count()) {
    s_leds[i] = CRGB(r, g, b);
  }
}

void LedBus::setPacked(uint16_t i, const uint8_t *ch) {
  if (ch != nullptr) {
    setRgb(i, ch[0], ch[1], ch[2]);
  }
}

void LedBus::setOutputPacked(uint8_t out, const uint8_t *ch, uint16_t len) {
  if (ch == nullptr || out >= PixelMap::outputCount()) {
    return;
  }
  const uint16_t first = PixelMap::outputPixelOffset(out);
  const uint16_t n = PixelMap::outputPixelCount(out);
  const uint8_t cpp = PixelMap::cfg().channelsPerPixel;
  for (uint16_t p = 0; p < n; ++p) {
    const uint16_t o = static_cast<uint16_t>(p * cpp);
    if (static_cast<uint16_t>(o + 3) <= len) {
      setPacked(static_cast<uint16_t>(first + p), ch + o);
    } else {
      setRgb(static_cast<uint16_t>(first + p), 0, 0, 0);
    }
  }
}

void LedBus::fillRgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t n = count();
  for (uint16_t i = 0; i < n; ++i) {
    s_leds[i] = CRGB(r, g, b);
  }
}

void LedBus::clear() { fillRgb(0, 0, 0); }

#endif
