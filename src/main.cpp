#include <Arduino.h>
#include <FastLED.h>
#include <SD.h>
#include <SPI.h>

#include "board_matrix.h"
#include "log.h"
#include "version.h"
#include "wifi_setup.h"

static CRGB leds[kLedCount];

static uint16_t xy(uint8_t x, uint8_t y) {
  if (kMatrixSerpentine && (x & 1)) {
    return static_cast<uint16_t>(x * kMatrixHeight + (kMatrixHeight - 1 - y));
  }
  return static_cast<uint16_t>(x * kMatrixHeight + y);
}

static const char *sdCardTypeName(uint8_t cardType) {
  switch (cardType) {
  case CARD_NONE:
    return "none";
  case CARD_MMC:
    return "MMC";
  case CARD_SD:
    return "SDSC";
  case CARD_SDHC:
    return "SDHC";
  default:
    return "unknown";
  }
}

static void listSdRoot() {
  File root = SD.open("/");
  if (!root) {
    LOG_C("sd", "root open failed");
    return;
  }

  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    LOG_V("sd", "%s %s bytes=%u", entry.isDirectory() ? "dir" : "file",
          entry.name(), static_cast<unsigned>(entry.size()));
    entry.close();
    ++count;
    if (count >= 12) {
      LOG_V("sd", "root listing truncated");
      break;
    }
  }
  root.close();
  LOG_V("sd", "root entries=%d", count);
}

static void initSd() {
  LOG_V("sd", "spi cs=%u mosi=%u clk=%u miso=%u hz=%u", kSdCs, kSdMosi, kSdClk,
        kSdMiso, static_cast<unsigned>(kSdSpiHz));
  SPI.begin(kSdClk, kSdMiso, kSdMosi, kSdCs);
  if (!SD.begin(kSdCs, SPI, kSdSpiHz)) {
    LOG_C("sd", "mount failed");
    return;
  }

  const uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    LOG_C("sd", "no card");
    return;
  }

  const uint64_t sizeMb = SD.cardSize() / (1024ULL * 1024ULL);
  LOG_V("sd", "mount OK type=%s size_MB=%u", sdCardTypeName(cardType),
        static_cast<unsigned>(sizeMb));
  listSdRoot();
}

void setup() {
  Log::begin(115200, kLogLevelDefault);
  LOG_V("boot", "ESP32-S3-Matrix v%s", kFirmwareVersion);
  LOG_V("log", "level=%u (0=off 1=critical 2=verbose)",
        static_cast<unsigned>(Log::level()));
  WifiSetup::begin();
  initSd();

  FastLED.addLeds<WS2812B, kLedPin, GRB>(leds, kLedCount);
  FastLED.setBrightness(kBrightness);
  FastLED.clear(true);
  LOG_V("led", "init pin=%u count=%u brightness=%u", kLedPin, kLedCount,
        kBrightness);
}

void loop() {
  Log::service();
  WifiSetup::service();
  static uint8_t hue = 0;

  for (uint8_t x = 0; x < kMatrixWidth; ++x) {
    const uint8_t columnHue =
        static_cast<uint8_t>(hue + x * (256 / kMatrixWidth));
    for (uint8_t y = 0; y < kMatrixHeight; ++y) {
      leds[xy(x, y)] = CHSV(columnHue, 255, 255);
    }
  }

  FastLED.show();
  hue++;
  delay(25);
}
