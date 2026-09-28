#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Seeed Studio XIAO ESP32-C6 (4 MB flash, no PSRAM, native USB CDC).
// Silk as every XIAO: D0 data GPIO0, D1 clock GPIO1, D7–D10 SD CS/SCK/MISO/MOSI.
// The core variant drives GPIO 3 (RF switch power) and 14 (antenna select).
#define WHIP_BOARD_ID "seeed-xiao-esp32-c6"
static constexpr char kBoardId[] = WHIP_BOARD_ID;
static constexpr char kBoardChip[] = "esp32c6";
static constexpr char kBoardFlashClass[] = "4mb-cdc";
static constexpr uint8_t kGpioMax = 30;
static constexpr uint8_t kCpuCount = 1;
static constexpr uint8_t kServiceCore = 0;
static constexpr BoardRadio kRadioKind = BoardRadio::Wifi;

static inline bool boardReservedGpio(uint8_t pin) {
  // RF switch 3/14; USB 12/13; in-package flash 24–30.
  if (pin == 3 || pin == 12 || pin == 13 || pin == 14) {
    return true;
  }
  if (pin >= 24 && pin <= 30) {
    return true;
  }
  return false;
}

static constexpr uint8_t kLedPin = 0;
static constexpr uint8_t kMatrixWidth = 0;
static constexpr uint8_t kMatrixHeight = 0;
static constexpr uint16_t kLedCount = 64;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 0;
static constexpr uint8_t kPatchMaxOutputs = 2;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 17;
static constexpr uint8_t kSdMosi = 18;
static constexpr uint8_t kSdClk = 19;
static constexpr uint8_t kSdMiso = 20;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
