#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Seeed Studio XIAO ESP32-C5 (8 MB flash + 8 MB PSRAM, native USB CDC).
// Silk: D0 data GPIO1, D1 clock GPIO0, D7–D10 SD CS/SCK/MISO/MOSI.
static constexpr char kBoardId[] = "seeed-xiao-esp32-c5";
static constexpr char kBoardChip[] = "esp32c5";
static constexpr char kBoardFlashClass[] = "8mb-psram8-cdc";
static constexpr uint8_t kGpioMax = 28;
static constexpr uint8_t kCpuCount = 1;
static constexpr uint8_t kServiceCore = 0;
static constexpr BoardRadio kRadioKind = BoardRadio::Wifi;

static inline bool boardReservedGpio(uint8_t pin) {
  if (pin == 13 || pin == 14) {
    return true;
  }
  if (pin >= 15 && pin <= 22) {
    return true;
  }
  return false;
}

static constexpr uint8_t kLedPin = 1;
static constexpr uint8_t kMatrixWidth = 0;
static constexpr uint8_t kMatrixHeight = 0;
static constexpr uint16_t kLedCount = 64;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 0;
static constexpr uint8_t kPatchMaxOutputs = 2;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 12;
static constexpr uint8_t kSdMosi = 10;
static constexpr uint8_t kSdClk = 8;
static constexpr uint8_t kSdMiso = 9;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
