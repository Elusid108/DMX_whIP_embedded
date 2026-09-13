#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pixel_map.h"

// Playback engine: companion DMXREC (.dmx) → Matrix-scale ring. Does not
// drive LEDs. Console owns the fixture patch; this node copies its universe
// 1:1 onto the strip (N pixels × chips, GPIO from the board profile).
//
// Live is drop-to-latest. Playback pauses the output clock on underrun and
// does not invent frames. Do not mix those policies.
//
// FastLED.show() only in main. Peek/copyFrame consume the reader ring.

static constexpr uint8_t kPlayRingSlots = 4;
static constexpr uint16_t kPlayMaxPixels = kMatrixPixelMap.pixelCount;
static constexpr uint8_t kPlayMaxChips = kMatrixPixelMap.channelsPerPixel;
static constexpr uint16_t kPlayMaxPayload =
    static_cast<uint16_t>(kPlayMaxPixels * kPlayMaxChips);

class Playback {
public:
  static void begin();
  static void service();
  static void start();
  static void stop();

  static bool hasFile();
  static bool running();
  static bool underrun();
  static uint8_t available();

  // Head-of-ring show timestamp (t_ms * 1000). False if empty (underrun).
  static bool peek(uint32_t &t_us);

  // Pointer into the ring; valid until pop() or copyFrame(). Null if empty.
  static const uint8_t *peekPayload(size_t &n, uint32_t &t_us);

  // Copy RGB payload and pop. Fills t_us when non-null. False if empty or n
  // is smaller than the payload.
  static bool copyFrame(uint8_t *rgb, size_t n, uint32_t *t_us = nullptr);

  static bool pop();
  static uint32_t tUs();
  static uint16_t fps();
  static uint32_t payloadBytes();
  static const char *path();
};
