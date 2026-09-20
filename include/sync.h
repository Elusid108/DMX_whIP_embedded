#pragma once

#include <stdint.h>

// Live ArtSync / E1.31 fence and playback cue UDP bus (Matrix-scale).
//
// Live: Art-Net ArtSync (opcode 0x5200 on UDP 6454) and E1.31 synchronization
// PDUs raise a fence. The jitter buffer stays drop-to-latest (buf 0–3). When
// a sync PDU has been seen recently, the render path releases the current
// latest live frame on that fence instead of free-running only on portal FPS.
// Sync-only packets must not call LiveInput::push (portal stays up if no DMX).
// No sync for ~2 s → same as before (portal FPS + buf 0–3).
//
// Playback cue bus: UDP 4777, multicast 239.255.77.77 (unicast also accepted).
// Does not collide with Art-Net 6454, sACN 5568, or HTTP 80.
// Companion (or another node) emits play / pause / seek / tick. The show clock
// is the cue, not local millis(). Late nodes snap to the tick. Underrun pauses
// output and waits for the next cue — no invented frames.
//
// Default with no companion: auto-play when idle (lone Matrix). After a cue is
// heard, follow the bus. After kCueHoldMs with no cue packets, return to
// local auto-play. While following, a rolling show should keep sending Tick
// (or repeated Play) so every node shares the same t_ms / frame index.
//
// Cue packet — 16 bytes, little-endian, packed:
//
//   offset  size  field
//   0       4     magic "WHIP"
//   4       1     version (1)
//   5       1     opcode  CueOp
//   6       1     flags   kCueFlagHasTime | kCueFlagHasFrame
//   7       1     reserved (0)
//   8       4     t_ms    show-relative milliseconds (DMXREC t_ms)
//   12      4     frame   0-based DMXREC file record index
//
// Opcodes: Play=1 Pause=2 Seek=3 Tick=4.
// Play/Seek/Tick with time or frame set the target; Play also rolls transport;
// Pause freezes (optional time/frame still seeks). Tick is a silent metronome.

static constexpr uint16_t kCuePort = 4777;
static constexpr uint8_t kCueMcastA = 239;
static constexpr uint8_t kCueMcastB = 255;
static constexpr uint8_t kCueMcastC = 77;
static constexpr uint8_t kCueMcastD = 77;

static constexpr char kCueMagic0 = 'W';
static constexpr char kCueMagic1 = 'H';
static constexpr char kCueMagic2 = 'I';
static constexpr char kCueMagic3 = 'P';
static constexpr uint8_t kCueVersion = 1;
static constexpr uint8_t kCueVersion2 = 2;
static constexpr uint8_t kCuePktLen = 16;
static constexpr uint8_t kCuePktLenV2 = 20;

static constexpr uint8_t kCueOpPlay = 1;
static constexpr uint8_t kCueOpPause = 2;
static constexpr uint8_t kCueOpSeek = 3;
static constexpr uint8_t kCueOpTick = 4;

static constexpr uint8_t kCueFlagHasTime = 1u << 0;
static constexpr uint8_t kCueFlagHasFrame = 1u << 1;

static constexpr uint32_t kLiveSyncHoldMs = 2000;
static constexpr uint32_t kCueHoldMs = 4000;

#if defined(__GNUC__)
#define SYNC_PACKED __attribute__((packed))
#else
#define SYNC_PACKED
#endif

#pragma pack(push, 1)
struct CuePacket {
  char magic[4];
  uint8_t version;
  uint8_t opcode;
  uint8_t flags;
  uint8_t reserved;
  uint32_t t_ms;
  uint32_t frame;
} SYNC_PACKED;
#pragma pack(pop)

static_assert(sizeof(CuePacket) == kCuePktLen, "CuePacket");

#undef SYNC_PACKED

class Sync {
public:
  static void begin();
  static void service();

  static void onArtSync();
  static void onE131Sync(uint16_t universe);

  static bool liveSyncActive();
  static bool hasLiveFence();
  static bool takeLiveFence();

  static bool cueFollow();
  static bool cuePlaying();
  static bool cueSocketUp();
  static bool hasCuePulse();
  static bool takeCuePulse();
  static bool cueHasTime();
  static bool cueHasFrame();
  static uint32_t cueTargetMs();
  static uint32_t cueTargetFrame();

  static void onPlayFile(const char *path);
  static void noteLocalTrigger();
  static void noteLocalPause();
  static void noteAutoStart();
  static bool waitingForMaster();
  static bool isMaster();
  static bool hasGroup();
  static const char *groupId();
  static uint8_t memberCount();
  static const char *memberNameAt(uint8_t i);
};
