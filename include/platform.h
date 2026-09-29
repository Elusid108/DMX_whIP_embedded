#pragma once

#include <stddef.h>
#include <stdint.h>

// What differs between the ESP32 boards and the RP2040 / RP2350 player that
// is not a board pin. WHIP_HAS_NET is 0 on boards without a radio (set by
// the [rp] env); network code stays out of those builds.
#ifndef WHIP_HAS_NET
#define WHIP_HAS_NET 1
#endif

namespace Platform {

// "AA:BB:CC:DD:EE:FF": the Wi-Fi MAC on ESP32, the flash unique id (last six
// bytes) on RP. The companion names nodes by its last four hex digits.
void uniqueId(char *out, size_t n);
// Restart the chip now.
void restart();

} // namespace Platform
