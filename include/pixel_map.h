#pragma once

#include <stdint.h>

#include "board_matrix.h"

// Node identity + 1:1 pixel map (not a renderer).
//
// Channel / universe convention:
//   Art-Net universe: 0-based (Matrix start = 0). Resolume "universe 1" is
//     often this value.
//   sACN universe: 1-based (stored as Art-Net start + 1).
//   DMX channel: 1-based lighting. Channel 1 is the first byte of a universe
//     payload (offset 0).
//   Logical DMX per pixel: R,G,B[,W][,C]. colorOrder is the IC wire order.
// No serpentine remap — Resolume owns the fixture patch.

enum class LedChipset : uint8_t {
  WS2812B = 0,
  WS2812 = 1,
  SK6812 = 2,
  WS2811 = 3,
  WS2813 = 4,
  WS2815 = 5,
  WS2816 = 6,
  WS2818 = 7,
  SK6822 = 8,
  TM1803 = 9,
  TM1804 = 10,
  TM1809 = 11,
  TM1829 = 12,
  UCS1903 = 13,
  UCS1903B = 14,
  UCS1904 = 15,
  UCS2903 = 16,
  APA106 = 17,
  PL9823 = 18,
  SM16703 = 19,
  GE8822 = 20,
  GW6205 = 21,
  GS1903 = 22,
  LPD1886 = 23,
  APA102 = 24,
  SK9822 = 25,
  HD107S = 26,
  WS2801 = 27,
  LPD8806 = 28,
  P9813 = 29,
  LPD6803 = 30
};

enum class LedWire : uint8_t {
  Clockless = 0,
  Apa102 = 1,
  Ws2801 = 2,
  Lpd8806 = 3,
  P9813 = 4,
  Lpd6803 = 5
};

static constexpr uint16_t kDmxUniverseSize = 512;
static constexpr uint8_t kClockGpioNone = 0;
static constexpr uint8_t kRgbChannels = 3;
static constexpr uint8_t kMaxChannelsPerPixel = 5;
static constexpr uint8_t kMaxUniverses = 6;
static constexpr uint16_t kMatrixArtNetUniverse = 0;
static constexpr uint16_t kMatrixSacnUniverse = 1;
static constexpr uint16_t kMatrixStartChannel = 1;
static constexpr uint16_t kLedCountMax = 1024;

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
  char colorOrder[6];
  uint8_t dataGpio;
  uint8_t clockGpio;
  uint16_t pixelCount;
  uint16_t startArtNetUniverse;
  uint16_t startSacnUniverse;
  uint16_t startChannel;
  uint8_t chipsPerPixel;
  uint8_t channelsPerPixel;
  bool white;
  bool cct;
  bool splitAcrossUniverses;
  uint8_t brightnessDefault;
};

struct PixelMapSet {
  LedChipset chipset;
  char colorOrder[6];
  uint8_t dataGpio;
  uint8_t clockGpio;
  uint16_t pixelCount;
  uint16_t startArtNetUniverse;
  uint16_t startChannel;
  bool white;
  bool cct;
};

static constexpr PixelMapCfg kMatrixPixelMap = {
    LedChipset::WS2812B,
    {'g', 'r', 'b', 0, 0, 0},
    kLedPin,
    kClockGpioNone,
    kLedCount,
    kMatrixArtNetUniverse,
    kMatrixSacnUniverse,
    kMatrixStartChannel,
    1,
    kRgbChannels,
    false,
    false,
    false,
    kBrightnessDefault,
};

class PixelMap {
public:
  static void begin();
  static const PixelMapCfg &cfg();
  static uint16_t channelCount();
  static uint16_t universeSpan();
  static uint16_t firstUniversePixels();
  static const char *chipsetName();
  static const char *colorOrderName();
  static LedWire wireKind();
  static bool needsClock();
  static bool needsClock(LedChipset chip);
  static bool clocklessUnits(uint8_t &t1, uint8_t &t2, uint8_t &t3);
  static bool parseChipset(const char *s, LedChipset &out);
  static bool parseOrder(const char *s, char out[6], bool white, bool cct);
  static bool validOrder(const char *s, bool white, bool cct);
  static bool validDataGpio(uint8_t pin);
  static bool validClockGpio(uint8_t pin, uint8_t dataGpio, bool required);
  static bool validCount(uint16_t n);
  static bool set(const PixelMapSet &in, bool save);
  static bool pixelOrigin(uint16_t pixelIndex, uint16_t &uniOff, uint16_t &ch1);
  static bool lookupRgb(uint16_t pixelIndex, PixelRgbAddr &out);
};
