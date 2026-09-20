#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Espressif ESP32-C5-DevKitC-1-N8R4 (8 MB flash + 4 MB QSPI PSRAM).
static constexpr char kBoardId[] = "espressif-c5-devkitc1-n8r4";
static constexpr char kBoardChip[] = "esp32c5";
static constexpr char kBoardFlashClass[] = "8mb-psram";
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

static constexpr uint8_t kLedPin = 24;
static constexpr uint8_t kMatrixWidth = 5;
static constexpr uint8_t kMatrixHeight = 5;
static constexpr uint16_t kLedCount = kMatrixWidth * kMatrixHeight;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 0;
static constexpr uint8_t kPatchMaxOutputs = 2;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 10;
static constexpr uint8_t kSdMosi = 7;
static constexpr uint8_t kSdClk = 6;
static constexpr uint8_t kSdMiso = 2;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
