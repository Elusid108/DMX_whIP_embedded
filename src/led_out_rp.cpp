// RP2040 / RP2350 clockless LED output: one PIO state machine + DMA channel
// per output (RpPioTx), bound to its pin at runtime.
#if defined(ARDUINO_ARCH_RP2040)

#include "led_out.h"

#include <cstring>

#include "rp_pio_tx.h"

namespace {

static RpPioTx s_tx[kPatchMaxOutputs];
// Wire bytes per output, kept while DMA reads them. Outputs never overlap:
// output pixels [off, off + n) use the slice that starts at off.
static uint8_t s_wire[kLedCountMax * kMaxChannelsPerPixel];

} // namespace

bool LedOut::bind(uint8_t out, uint8_t pin, LedChipset chip) {
  if (out >= kPatchMaxOutputs) {
    return false;
  }
  uint8_t t1 = 2;
  uint8_t t2 = 5;
  uint8_t t3 = 3;
  PixelMap::clocklessUnits(chip, t1, t2, t3);
  return s_tx[out].beginClockless(pin, t1, t2, t3);
}

void LedOut::release(uint8_t out) {
  if (out < kPatchMaxOutputs) {
    s_tx[out].end();
  }
}

bool LedOut::bound(uint8_t out) {
  return out < kPatchMaxOutputs && s_tx[out].bound();
}

void LedOut::setRange(uint8_t, const LedFrame &, uint16_t, uint16_t) {}

void LedOut::show(uint8_t out, const LedFrame &f, uint16_t off, uint16_t n,
                  bool) {
  if (n == 0 || !bound(out) || static_cast<uint32_t>(off) + n > kLedCountMax) {
    return;
  }
  // The last frame may still be reading this slice.
  s_tx[out].wait();
  uint8_t *w = s_wire + static_cast<size_t>(off) * kMaxChannelsPerPixel;
  size_t o = 0;
  for (uint16_t p = 0; p < n; ++p) {
    uint8_t ch = f.ch[off + p] ? f.ch[off + p] : 3;
    if (ch > kMaxChannelsPerPixel) {
      ch = kMaxChannelsPerPixel;
    }
    memcpy(w + o, f.px[off + p], ch);
    o += ch;
  }
  s_tx[out].send(w, o);
}

#endif
