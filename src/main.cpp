#include <Arduino.h>
#include <FastLED.h>
#include <SD.h>
#include <SPI.h>

#include "artnet_rx.h"
#include "board_matrix.h"
#include "live_input.h"
#include "log.h"
#include "version.h"
#include "wifi_setup.h"

static CRGB leds[kLedCount];
static constexpr uint32_t kLedIntervalMs = 25;

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

static void renderArtNet() {
  const uint8_t *d = ArtNetRx::dmx();
  const uint16_t len = ArtNetRx::dmxLen();
  for (uint16_t p = 0; p < kLedCount; ++p) {
    const uint16_t i = static_cast<uint16_t>(p * 3);
    if (i + 2 < len) {
      leds[p] = CRGB(d[i], d[i + 1], d[i + 2]);
    } else {
      leds[p] = CRGB::Black;
    }
  }
  ArtNetRx::consume();
}

void setup() {
  Log::begin(115200, kLogLevelDefault);
  LOG_V("boot", "ESP32-S3-Matrix v%s", kFirmwareVersion);
  LOG_V("log", "level=%u (0=off 1=critical 2=verbose)",
        static_cast<unsigned>(Log::level()));
  WifiSetup::begin();
  ArtNetRx::begin();
  initSd();

  FastLED.addLeds<WS2812B, kLedPin, GRB>(leds, kLedCount);
  FastLED.setBrightness(kBrightness);
  FastLED.clear(true);
  LOG_V("led", "init pin=%u count=%u brightness=%u", kLedPin, kLedCount,
        kBrightness);
}

void loop() {
  Log::service();
  ArtNetRx::service();
  WifiSetup::service();

  static uint32_t lastShow = 0;
  const uint32_t now = millis();
  if (now - lastShow < kLedIntervalMs) {
    yield();
    return;
  }
  lastShow = now;

  if (LiveInput::active()) {
    renderArtNet();
  } else {
    FastLED.clear();
  }
  FastLED.show();
}
