#pragma once

#include <stdint.h>

#include "log.h"

// Waveshare ESP32-S3-Matrix (ESP32-S3FH4R2).
static constexpr uint8_t kLedPin = 14;
static constexpr uint8_t kMatrixWidth = 8;
static constexpr uint8_t kMatrixHeight = 8;
static constexpr uint16_t kLedCount = kMatrixWidth * kMatrixHeight;
static constexpr uint8_t kBrightness = 10;

// SPI microSD (not SDMMC). 3.3 V module only.
static constexpr uint8_t kSdCs = 7;
static constexpr uint8_t kSdMosi = 6;
static constexpr uint8_t kSdClk = 5;
static constexpr uint8_t kSdMiso = 4;
static constexpr uint32_t kSdSpiHz = 4000000;
static constexpr LogLevel kLogLevelDefault = LogLevel::Verbose;
