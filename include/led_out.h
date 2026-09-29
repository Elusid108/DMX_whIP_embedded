#pragma once

#include <stdint.h>

#include <FastLED.h>

#include "pixel_map.h"

// Clockless (one-wire) LED output behind LedBus. One implementation per chip
// family: led_out_esp.cpp (RMT) and led_out_rp.cpp (PIO + DMA). Clocked chips
// go through ClockedTx instead.
//
// LedBus owns the frame: px[i] is pixel i in wire order, already scaled by
// brightness, ch[i] bytes long; leds[i] mirrors its first three bytes.
struct LedFrame {
  CRGB *leds;
  uint8_t (*px)[kMaxChannelsPerPixel];
  uint8_t *ch;
};

class LedOut {
public:
  // Take the pin for this output, with the chip's bit timing. False when the
  // platform has no channel left.
  static bool bind(uint8_t out, uint8_t pin, LedChipset chip);
  static void release(uint8_t out);
  static bool bound(uint8_t out);
  // The pixel range of a bound output changed (same pin).
  static void setRange(uint8_t out, const LedFrame &f, uint16_t off, uint16_t n);
  // Send pixels [off, off + n). wide: some pixel has more than 3 channels.
  static void show(uint8_t out, const LedFrame &f, uint16_t off, uint16_t n,
                   bool wide);
};
