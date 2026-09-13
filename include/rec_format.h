#pragma once

#include <stddef.h>
#include <stdint.h>

// Recording file format v1 — packed on-disk structs only (no SD I/O).
//
// Replaces archive v0: 6-byte magic "DMXREC", 4-byte frame count, then a
// 10-byte per-frame header with no timestamps, CRC, map, or index.
//
// Endianness: all multi-byte fields are little-endian. ESP32 is LE, so these
// structs may be written/read as raw bytes on this chip. A big-endian host
// must byteswap.
//
// Show clock: RecFramePrefix::t_us is microseconds from show t=0, not wall
// time and not a node's millis() origin. Nodes share this timeline via cues
// (WS7); they must not free-run from local boot.
//
// Layout:
//   [ RecFileHeader          ]  64 bytes at offset 0
//   [ map blob               ]  header.map_bytes (0 when identity)
//   [ RecFrame × frame_count ]  prefix | payload | crc32
//   [ RecIndexHeader         ]  at header.index_offset if kRecFlagHasIndex
//   [ RecIndexEntry × N      ]
//
// Suggested SD name: *.dwr  (do not reuse archive *.dmx / "DMXREC").

#if defined(__GNUC__)
#define REC_PACKED __attribute__((packed))
#else
#define REC_PACKED
#endif

static constexpr uint32_t recFourcc(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

static constexpr uint32_t kRecMagic = recFourcc('D', 'W', 'R', '1');
static constexpr uint32_t kRecIndexMagic = recFourcc('D', 'W', 'I', 'X');
static constexpr uint16_t kRecFormatVersion = 1;

// Flags in RecFileHeader::flags (OR together).
static constexpr uint16_t kRecFlagHasIndex = 1u << 0;
static constexpr uint16_t kRecFlagRgbw = 1u << 1;

// RecFileHeader::map_kind. Identity needs no blob: payload is packed pixels
// 0..pixel_count-1 in output order. Custom blob is RecMapEntry[pixel_count].
static constexpr uint8_t kRecMapIdentity = 0;
static constexpr uint8_t kRecMapCustom = 1;

// IEEE CRC-32 (ISO 3309 / PNG / Ethernet, reflected).
// Scope: see recFrameCrc32 — prefix (t_us, size) + payload, not the CRC word.
// Header CRC: all RecFileHeader bytes except header_crc32 itself.
static constexpr uint32_t kRecCrcPoly = 0xEDB88320u;
static constexpr uint32_t kRecCrcInit = 0xFFFFFFFFu;
static constexpr uint32_t kRecCrcXorOut = 0xFFFFFFFFu;

static constexpr uint32_t kRecHeaderBytes = 64;
static constexpr uint32_t kRecFramePrefixBytes = 8;
static constexpr uint32_t kRecFrameCrcBytes = 4;
static constexpr uint32_t kRecFrameOverheadBytes =
    kRecFramePrefixBytes + kRecFrameCrcBytes;
static constexpr uint32_t kRecIndexHeaderBytes = 16;
static constexpr uint32_t kRecIndexEntryBytes = 16;

// uint32 t_us wraps at 2^32 us (~71.58 min). v1 files must stay below that.
static constexpr uint32_t kRecMaxDurationUs = 0xFFFFFFFFu;

#pragma pack(push, 1)

struct RecFileHeader {
  uint32_t magic;            // kRecMagic ("DWR1")
  uint16_t version;          // kRecFormatVersion
  uint16_t flags;            // kRecFlag*
  uint16_t fps;              // show rate (20/30/40/60 typical; 0 invalid)
  uint16_t chips_per_pixel;  // 3 = RGB, 4 = RGBW (RGBW also sets kRecFlagRgbw)
  uint32_t pixel_count;      // payload pixels; not a 100k compile-time buffer
  uint32_t frame_count;
  uint64_t index_offset;     // 0 if no index; else file offset of RecIndexHeader
  uint8_t map_kind;          // kRecMapIdentity or kRecMapCustom
  uint8_t split_universes;   // 1 = identity map wraps at DMX 512
  uint16_t start_universe;   // identity: first pixel's universe
  uint16_t start_channel;    // identity: first pixel's first channel (0..511)
  uint16_t reserved0;
  uint32_t map_bytes;        // blob size immediately after this header
  uint8_t reserved1[20];
  uint32_t header_crc32;     // CRC-32 of the 60 bytes before this field
} REC_PACKED;

// Present only when map_kind == kRecMapCustom (map_bytes = N * sizeof).
// Playback may ignore this and stream packed pixels to the strip in order.
struct RecMapEntry {
  uint16_t universe;
  uint16_t channel;
} REC_PACKED;

// Per-frame: [ RecFramePrefix | payload[size] | crc32 ].
struct RecFramePrefix {
  uint32_t t_us;  // show-relative microseconds (t=0 is the start of the show)
  uint32_t size;  // payload bytes; typically pixel_count * chips_per_pixel
} REC_PACKED;

struct RecIndexHeader {
  uint32_t magic;        // kRecIndexMagic ("DWIX")
  uint32_t entry_count;  // equals RecFileHeader::frame_count
  uint32_t reserved[2];
} REC_PACKED;

struct RecIndexEntry {
  uint32_t t_us;         // copy of that frame's show-relative timestamp
  uint32_t reserved;
  uint64_t file_offset;  // offset of that frame's RecFramePrefix
} REC_PACKED;

#pragma pack(pop)

static_assert(sizeof(RecFileHeader) == kRecHeaderBytes, "RecFileHeader");
static_assert(sizeof(RecMapEntry) == 4, "RecMapEntry");
static_assert(sizeof(RecFramePrefix) == kRecFramePrefixBytes, "RecFramePrefix");
static_assert(sizeof(RecIndexHeader) == kRecIndexHeaderBytes, "RecIndexHeader");
static_assert(sizeof(RecIndexEntry) == kRecIndexEntryBytes, "RecIndexEntry");

inline bool recMagicOk(uint32_t magic) { return magic == kRecMagic; }

inline bool recMagicOk(const RecFileHeader &h) { return recMagicOk(h.magic); }

inline bool recVersionOk(const RecFileHeader &h) {
  return h.version == kRecFormatVersion;
}

// First 6 bytes of archive v0 ("DMXREC"). Not a valid v1 file.
inline bool recIsLegacyDmxrec(const uint8_t *p) {
  return p[0] == 'D' && p[1] == 'M' && p[2] == 'X' && p[3] == 'R' &&
         p[4] == 'E' && p[5] == 'C';
}

inline bool recHasIndex(const RecFileHeader &h) {
  return (h.flags & kRecFlagHasIndex) != 0 && h.index_offset != 0;
}

inline uint32_t recPayloadBytes(uint32_t pixel_count, uint16_t chips_per_pixel) {
  return pixel_count * static_cast<uint32_t>(chips_per_pixel);
}

inline uint32_t recPayloadBytes(const RecFileHeader &h) {
  return recPayloadBytes(h.pixel_count, h.chips_per_pixel);
}

inline uint32_t recFrameOnDiskBytes(uint32_t payload_size) {
  return kRecFrameOverheadBytes + payload_size;
}

inline uint32_t recMapBlobOffset() { return kRecHeaderBytes; }

inline uint32_t recFrameDataOffset(const RecFileHeader &h) {
  return kRecHeaderBytes + h.map_bytes;
}

// Continues a previous recCrc32 / recCrc32Update result (finalized form).
inline uint32_t recCrc32Update(uint32_t crc, const uint8_t *data, size_t len) {
  crc ^= kRecCrcXorOut;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 1u) ? (crc >> 1) ^ kRecCrcPoly : (crc >> 1);
    }
  }
  return crc ^ kRecCrcXorOut;
}

inline uint32_t recCrc32(const uint8_t *data, size_t len) {
  return recCrc32Update(kRecCrcInit ^ kRecCrcXorOut, data, len);
}

inline uint32_t recHeaderCrc32(const RecFileHeader &h) {
  return recCrc32(reinterpret_cast<const uint8_t *>(&h),
                  kRecHeaderBytes - sizeof(uint32_t));
}

// CRC-32 of prefix + payload (not the trailing crc32 word).
inline uint32_t recFrameCrc32(const RecFramePrefix &prefix,
                             const uint8_t *payload) {
  uint32_t crc =
      recCrc32(reinterpret_cast<const uint8_t *>(&prefix), sizeof(prefix));
  return recCrc32Update(crc, payload, prefix.size);
}

#undef REC_PACKED
