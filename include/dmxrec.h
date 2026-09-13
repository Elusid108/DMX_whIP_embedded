#pragma once

#include <stddef.h>
#include <stdint.h>

// Companion DMXREC (.dmx) on-disk layout. Matches
// DMX_whIP_companion src/services/shared/dmxRecording.js.
//
// Little-endian. ESP32 may read these structs as raw bytes.
//
//   [ DmxrecHeader  10 bytes ]  "DMXREC" + uint32 frame_count
//   [ DmxrecFrame × N        ]  522 bytes each
//
// Frame: t_ms | universe | protocol | 512 DMX slots.
// Protocol 0 = Art-Net, 1 = sACN. Mapping lives in the console;
// the node copies its universe 1:1 onto the strip.

#if defined(__GNUC__)
#define DMXREC_PACKED __attribute__((packed))
#else
#define DMXREC_PACKED
#endif

static constexpr uint32_t kDmxrecHeaderBytes = 10;
static constexpr uint32_t kDmxrecFrameBytes = 522;
static constexpr uint32_t kDmxrecPrefixBytes = 10;
static constexpr uint32_t kDmxrecDmxBytes = 512;
static constexpr uint16_t kDmxrecProtoArtNet = 0;
static constexpr uint16_t kDmxrecProtoSacn = 1;

#pragma pack(push, 1)

struct DmxrecHeader {
  char magic[6];
  uint32_t frame_count;
} DMXREC_PACKED;

struct DmxrecFramePrefix {
  uint32_t t_ms;
  uint32_t universe;
  uint16_t protocol;
} DMXREC_PACKED;

#pragma pack(pop)

static_assert(sizeof(DmxrecHeader) == kDmxrecHeaderBytes, "DmxrecHeader");
static_assert(sizeof(DmxrecFramePrefix) == kDmxrecPrefixBytes,
              "DmxrecFramePrefix");

inline bool dmxrecMagicOk(const char magic[6]) {
  return magic[0] == 'D' && magic[1] == 'M' && magic[2] == 'X' &&
         magic[3] == 'R' && magic[4] == 'E' && magic[5] == 'C';
}

inline bool dmxrecMagicOk(const DmxrecHeader &h) {
  return dmxrecMagicOk(h.magic);
}

inline uint32_t dmxrecFileBytes(uint32_t frame_count) {
  return kDmxrecHeaderBytes + frame_count * kDmxrecFrameBytes;
}

inline uint32_t dmxrecFrameOffset(uint32_t index) {
  return kDmxrecHeaderBytes + index * kDmxrecFrameBytes;
}

#undef DMXREC_PACKED
