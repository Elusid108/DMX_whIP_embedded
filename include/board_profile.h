#pragma once

#include <stdint.h>

#include "board_matrix.h"

// Waveshare ESP32-S3-Matrix compile-time identity. Extra PIO envs later
// swap the defaults; src/ stays shared. LED data pin is still FastLED
// template kLedPin. SD pins may be overlaid from NVS (0xFF = default).

static constexpr char kBoardId[] = "waveshare-s3-matrix";
static constexpr char kBoardChip[] = "esp32s3";
static constexpr char kBoardFlashClass[] = "4mb-qspi";
static constexpr uint8_t kGpioUnset = 0xFF;
static constexpr uint8_t kS3GpioMax = 48;

class BoardProfile {
public:
  static void begin();
  static const char *id();
  static const char *chip();
  static const char *flashClass();
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
