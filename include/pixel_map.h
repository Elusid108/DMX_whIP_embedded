#pragma once

#include <stdint.h>

#include "board_matrix.h"

// Node identity + 1:1 pixel map (not a renderer).
//
// Channel / universe convention:
//   Art-Net universe: 0-based (Matrix start = 0). Resolume "universe 1" is
//     often this value.
//   sACN universe: 1-based (Matrix start = 1).
//   DMX channel: 1-based lighting. Channel 1 is the first byte of a universe
//     payload (offset 0). Pixel i RGB occupies startChannel + i * 3 + {0,1,2}
//     when the block fits in one universe (Matrix: 64 px / 192 ch).
// Protocol payload is RGB; colorOrder is FastLED wire order (GRB here).
// No serpentine remap — Resolume owns the fixture patch.

enum class LedChipset : uint8_t { WS2812B = 0 };

enum class LedColorOrder : uint8_t { GRB = 0 };

static constexpr uint16_t kDmxUniverseSize = 512;
static constexpr uint8_t kClockGpioNone = 0;
static constexpr uint8_t kRgbChannels = 3;
static constexpr uint16_t kMatrixArtNetUniverse = 0;
static constexpr uint16_t kMatrixSacnUniverse = 1;
static constexpr uint16_t kMatrixStartChannel = 1;

struct PixelChan {
  uint16_t artNetUniverse;
  uint16_t sacnUniverse;
  uint16_t channel;
};

struct PixelRgbAddr {
  PixelChan r;
  PixelChan g;
  PixelChan b;
};

struct PixelMapCfg {
  LedChipset chipset;
  LedColorOrder colorOrder;
  uint8_t dataGpio;
  uint8_t clockGpio;
  uint16_t pixelCount;
  uint16_t startArtNetUniverse;
  uint16_t startSacnUniverse;
  uint16_t startChannel;
  uint8_t chipsPerPixel;
  uint8_t channelsPerPixel;
  bool splitAcrossUniverses;
  uint8_t brightnessDefault;
};

static constexpr PixelMapCfg kMatrixPixelMap = {
    LedChipset::WS2812B,
    LedColorOrder::GRB,
    kLedPin,
    kClockGpioNone,
    kLedCount,
    kMatrixArtNetUniverse,
    kMatrixSacnUniverse,
    kMatrixStartChannel,
    1,
    kRgbChannels,
    false,
    kBrightnessDefault,
};

class PixelMap {
public:
  static void begin();
  static const PixelMapCfg &cfg();
  static uint16_t channelCount();
  static const char *chipsetName();
  static const char *colorOrderName();
  static bool lookupRgb(uint16_t pixelIndex, PixelRgbAddr &out);
};
