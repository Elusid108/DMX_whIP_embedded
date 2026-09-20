#pragma once

#include <stdint.h>

#include "board_types.h"
#include "boards/select.h"

// Compile-time board identity. Extra PIO envs swap the selected header
// via BOARD_PROFILE_*. src/ stays shared. LED data pin/count come from
// PixelMap (NVS overlay). SD pins may be overlaid from NVS (0xFF = default).

static constexpr uint8_t kGpioUnset = 0xFF;

class BoardProfile {
public:
  static void begin();
  static const char *id();
  static const char *chip();
  static const char *flashClass();
  static BoardRadio radioKind();
  static uint8_t gpioMax();
  static uint8_t cpuCount();
  static uint8_t serviceCore();
  static bool reservedGpio(uint8_t pin);
  static uint8_t ledPin();
  static uint16_t ledCount();
  static uint8_t sdCs();
  static uint8_t sdMosi();
  static uint8_t sdClk();
  static uint8_t sdMiso();
  static uint32_t sdSpiHz();
  static bool validGpio(uint8_t pin);
  static bool setSdPins(uint8_t cs, uint8_t mosi, uint8_t clk, uint8_t miso,
                        bool save);
};
