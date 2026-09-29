#pragma once

#include <stdint.h>

// None: no network at all (RP2040 / RP2350 standalone player).
enum class BoardRadio : uint8_t { Wifi = 0, Eth = 1, Hosted = 2, None = 3 };
