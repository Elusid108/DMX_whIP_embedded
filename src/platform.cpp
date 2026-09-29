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

void restart() {
#if defined(ARDUINO_ARCH_RP2040)
  rp2040.reboot();
#else
  ESP.restart();
#endif
}

} // namespace Platform
