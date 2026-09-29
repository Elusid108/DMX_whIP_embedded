// ESP32 clockless LED output: FastLED's RMT5 driver, one controller per
// output, bound to its pin at runtime.
#if !defined(ARDUINO_ARCH_RP2040)

#include "led_out.h"

#include <Arduino.h>
#include <FastLED.h>

#include "platforms/esp/32/rmt_5/idf5_rmt.h"

namespace {

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

static void timings(LedChipset chip, int &t1, int &t2, int &t3) {
  uint8_t a = 2;
  uint8_t b = 5;
  uint8_t c = 3;
  PixelMap::clocklessUnits(chip, a, b, c);
  // Chip table stores old FastLED FMUL units (125 ns). RMT5 wants ns.
  t1 = static_cast<int>(a) * 125;
  t2 = static_cast<int>(b) * 125;
  t3 = static_cast<int>(c) * 125;
}

} // namespace

bool LedOut::bind(uint8_t out, uint8_t pin, LedChipset chip) {
  if (out >= kPatchMaxOutputs) {
    return false;
  }
  int t1 = 0;
  int t2 = 0;
  int t3 = 0;
  timings(chip, t1, t2, t3);
  s_ctrl[out].rebind(static_cast<int>(pin), t1, t2, t3);
  return s_ctrl[out].bound();
}

void LedOut::release(uint8_t out) {
  if (out < kPatchMaxOutputs && s_ctrl[out].bound()) {
    s_ctrl[out].release();
  }
}

bool LedOut::bound(uint8_t out) {
  return out < kPatchMaxOutputs && s_ctrl[out].bound();
}

void LedOut::setRange(uint8_t out, const LedFrame &f, uint16_t off, uint16_t n) {
  if (bound(out)) {
    s_ctrl[out].setLeds(f.leds + off, static_cast<int>(n));
  }
}

void LedOut::show(uint8_t out, const LedFrame &f, uint16_t off, uint16_t n,
                  bool wide) {
  if (n == 0 || !bound(out)) {
    return;
  }
  if (!wide) {
    s_ctrl[out].setLeds(f.leds + off, static_cast<int>(n));
    s_ctrl[out].showLeds(255);
    return;
  }
  // RGBW / RGBWC: the driver sends 3 bytes per "pixel", so the wire bytes are
  // laid over the CRGB array for the send, then the array is put back.
  static uint8_t scratch[kLedCountMax * 3];
  memset(scratch, 0, sizeof(scratch));
  uint32_t o = 0;
  const uint32_t cap = sizeof(scratch);
  for (uint16_t p = 0; p < n && o < cap; ++p) {
    const uint8_t ch = f.ch[off + p] ? f.ch[off + p] : 3;
    const uint32_t take = ch;
    if (o + take > cap) {
      break;
    }
    memcpy(scratch + o, f.px[off + p], take);
    o += take;
  }
  const uint16_t fake = static_cast<uint16_t>((o + 2u) / 3u);
  memcpy(f.leds + off, scratch,
         fake * 3u > (kLedCountMax - off) * 3u ? (kLedCountMax - off) * 3u
                                               : fake * 3u);
  s_ctrl[out].setLeds(f.leds + off, static_cast<int>(fake > n ? n : fake));
  if (fake > n) {
    s_ctrl[out].setLeds(f.leds + off, static_cast<int>(fake));
  }
  s_ctrl[out].showLeds(255);
  for (uint16_t p = 0; p < n; ++p) {
    f.leds[off + p] =
        CRGB(f.px[off + p][0], f.px[off + p][1], f.px[off + p][2]);
  }
  s_ctrl[out].setLeds(f.leds + off, static_cast<int>(n));
}

#endif
