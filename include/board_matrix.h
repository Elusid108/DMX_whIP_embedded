#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Waveshare ESP32-S3-Matrix (ESP32-S3FH4R2).
static constexpr char kBoardId[] = "waveshare-s3-matrix";
static constexpr char kBoardChip[] = "esp32s3";
static constexpr char kBoardFlashClass[] = "4mb-qspi";
static constexpr uint8_t kGpioMax = 48;
static constexpr uint8_t kCpuCount = 2;
static constexpr uint8_t kServiceCore = 1;
static constexpr BoardRadio kRadioKind = BoardRadio::Wifi;

static inline bool boardReservedGpio(uint8_t pin) {
  if (pin == 19 || pin == 20) {
    return true;
  }
  if (pin >= 26 && pin <= 32) {
    return true;
  }
  return false;
}

static constexpr uint8_t kLedPin = 14;
static constexpr uint8_t kMatrixWidth = 8;
static constexpr uint8_t kMatrixHeight = 8;
static constexpr uint16_t kLedCount = kMatrixWidth * kMatrixHeight;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 128;
static constexpr uint8_t kPatchMaxOutputs = 8;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 7;
static constexpr uint8_t kSdMosi = 6;
static constexpr uint8_t kSdClk = 5;
static constexpr uint8_t kSdMiso = 4;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
