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
// Restart into the USB drive that takes a UF2 (RP2040 / RP2350). False on
// boards without one; does not return when it works.
bool bootloader();
// True when the SD card's SPI bus can use these pins. An ESP32 routes SPI to
// any pin. RP2040 / RP2350 SPI0 has fixed choices (SCK 2/6/18/22, MOSI
// 3/7/19/23, MISO 0/4/16/20), and its core halts the board on any other pin,
// so every SD pin change is checked here first.
bool sdPinsOk(uint8_t mosi, uint8_t clk, uint8_t miso);

} // namespace Platform
