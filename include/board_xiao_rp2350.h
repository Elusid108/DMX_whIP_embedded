#pragma once

#include <stdint.h>

#include "board_types.h"
#include "log.h"

// Seeed Studio XIAO RP2350 (2 MB flash, no radio): standalone SD player, flashed
// by UF2, set up over USB serial. Silk as every XIAO: D0 data GPIO26, D1 clock
// GPIO27, D7-D10 SD CS/SCK/MISO/MOSI = GPIO1/2/4/3 (SPI0).
#define WHIP_BOARD_ID "seeed-xiao-rp2350"
static constexpr char kBoardId[] = WHIP_BOARD_ID;
static constexpr char kBoardChip[] = "rp2350";
static constexpr char kBoardFlashClass[] = "2mb-uf2";
static constexpr uint8_t kGpioMax = 29;
static constexpr uint8_t kCpuCount = 2;
static constexpr uint8_t kServiceCore = 1;
static constexpr BoardRadio kRadioKind = BoardRadio::None;

// Flash is on its own QSPI pins; every GPIO 0-29 is usable.
static inline bool boardReservedGpio(uint8_t) { return false; }

static constexpr uint8_t kLedPin = 26;
static constexpr uint8_t kMatrixWidth = 0;
static constexpr uint8_t kMatrixHeight = 0;
static constexpr uint16_t kLedCount = 64;
static constexpr uint8_t kBrightnessDefault = 10;
static constexpr uint8_t kBrightnessWarn = 0;
static constexpr uint8_t kPatchMaxOutputs = 4;
static constexpr uint8_t kPatchMaxSegments = 24;
static constexpr uint8_t kLiveUniSlots = 16;

// SPI0 microSD. 3.3 V module only.
static constexpr uint8_t kSdCs = 1;
static constexpr uint8_t kSdMosi = 3;
static constexpr uint8_t kSdClk = 2;
static constexpr uint8_t kSdMiso = 4;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
