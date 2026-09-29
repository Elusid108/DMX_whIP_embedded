#include "boards/select.h"
#include "version.h"

#define WHIP_STR2(x) #x
#define WHIP_STR(x) WHIP_STR2(x)

// Build tag in every image, ESP32 .bin and RP UF2 alike: the companion, the
// OTA handler and scripts/release.py read the board and version of an image
// by finding this string.
extern "C" __attribute__((used)) const char kWhipFwTag[] =
    "WHIPFW:" WHIP_BOARD_ID ":" WHIP_FW_VERSION ":" WHIP_STR(WHIP_FW_API) ";";
