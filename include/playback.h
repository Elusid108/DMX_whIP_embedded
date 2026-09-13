#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pixel_map.h"

// Playback engine (WS5): SD reader → Matrix-scale ring. Does not drive LEDs.
//
// Live is drop-to-latest. Playback pauses the output clock on underrun and
// does not invent frames. Do not mix those policies.
//
// WS6 hook (idle → playback, live preempts) — implement in main, not here:
//   setup(): SdInfo::begin(); Playback::begin();
//   loop(), after LiveInput::service():
//     if (LiveInput::active()) {
//       Playback::stop();           // skip SD I/O while live
//       /* existing live pop → CRGB */
//     } else {
//       Playback::service();
//       if (Playback::hasFile() && !Playback::running()) {
//         Playback::start();
//       }
//       uint32_t t_us = 0;
//       if (Playback::peek(t_us)) {
//         // Honor show-relative t_us / cue bus later (WS7). Until then, pop
//         // the next ring slot; if peek fails, pause (keep last LEDs).
//         uint8_t rgb[kPlayMaxPayload];
//         if (Playback::copyFrame(rgb, sizeof(rgb))) {
//           /* copy rgb → CRGB, same 1:1 path as live */
//         }
//       }
//     }
//     FastLED.show();  // render path only — never from this module
//
// peek / peekPayload / copyFrame / pop consume the reader ring, not the strip.

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

  // Head-of-ring show timestamp. False if empty (underrun while running).
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
