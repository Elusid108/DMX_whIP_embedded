#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Seeed Studio XIAO ESP32-C3 (4 MB flash, no PSRAM, native USB CDC).
// Silk as every XIAO: D0 data GPIO2, D1 clock GPIO3, D7–D10 SD CS/SCK/MISO/MOSI.
// GPIO 2, 8 and 9 are strapping pins: a load that pulls them low at reset
// can stop the board booting.
#define WHIP_BOARD_ID "seeed-xiao-esp32-c3"
static constexpr char kBoardId[] = WHIP_BOARD_ID;
static constexpr char kBoardChip[] = "esp32c3";
static constexpr char kBoardFlashClass[] = "4mb-cdc";
static constexpr uint8_t kGpioMax = 21;
static constexpr uint8_t kCpuCount = 1;
static constexpr uint8_t kServiceCore = 0;
static constexpr BoardRadio kRadioKind = BoardRadio::Wifi;

static inline bool boardReservedGpio(uint8_t pin) {
  // In-package flash 11–17; USB 18/19.
  if (pin >= 11 && pin <= 19) {
    return true;
  }
  return false;
}

static constexpr uint8_t kLedPin = 2;
static constexpr uint8_t kMatrixWidth = 0;
static constexpr uint8_t kMatrixHeight = 0;
static constexpr uint16_t kLedCount = 64;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 0;
static constexpr uint8_t kPatchMaxOutputs = 2;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 20;
static constexpr uint8_t kSdMosi = 10;
static constexpr uint8_t kSdClk = 8;
static constexpr uint8_t kSdMiso = 9;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
