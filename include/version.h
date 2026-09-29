#pragma once

#include <stdint.h>

// Macros so the OTA build tag (ota.cpp) can be one string literal.
#define WHIP_FW_VERSION "0.55.0"
#define WHIP_FW_API 3

static constexpr char kFirmwareVersion[] = WHIP_FW_VERSION;
static constexpr uint16_t kFirmwareApi = WHIP_FW_API;
