#include "platform.h"

#include <Arduino.h>

#include <cstdio>

#if WHIP_HAS_NET
#include <WiFi.h>
#endif

#if defined(ARDUINO_ARCH_RP2040)
#include "pico/unique_id.h"
#endif

namespace Platform {

void uniqueId(char *out, size_t n) {
  uint8_t id[6] = {0};
#if defined(ARDUINO_ARCH_RP2040)
  pico_unique_board_id_t uid;
  pico_get_unique_board_id(&uid);
  for (int i = 0; i < 6; ++i) {
    id[i] = uid.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 6 + i];
  }
#elif WHIP_HAS_NET
  WiFi.macAddress(id);
#endif
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", id[0], id[1], id[2], id[3],
           id[4], id[5]);
}

bool sdPinsOk(uint8_t mosi, uint8_t clk, uint8_t miso) {
#if defined(ARDUINO_ARCH_RP2040)
  const bool sck = clk == 2 || clk == 6 || clk == 18 || clk == 22;
  const bool tx = mosi == 3 || mosi == 7 || mosi == 19 || mosi == 23;
  const bool rx = miso == 0 || miso == 4 || miso == 16 || miso == 20;
  return sck && tx && rx;
#else
  (void)mosi;
  (void)clk;
  (void)miso;
  return true;
#endif
}

bool bootloader() {
#if defined(ARDUINO_ARCH_RP2040)
  rp2040.rebootToBootloader();
  return true;
#else
  return false;
#endif
}

void restart() {
#if defined(ARDUINO_ARCH_RP2040)
  rp2040.reboot();
#else
  ESP.restart();
#endif
}

} // namespace Platform
