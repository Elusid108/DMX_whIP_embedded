#pragma once

#include <IPAddress.h>
#include <stdint.h>

// Cue bus v3: shared network clock, roster, and group cues on UDP 4777.
//
// Every node (and the companion) broadcasts HELLO: role, api, which clock
// master it follows, its sync RTT, and a summary of the cue it is running.
// One clock master is elected (Show Host > companion > the incumbent master
// > lowest id). Everyone else measures the offset to it with NTP-style
// unicast PING/PONG and slews its clock (<= 0.5 %) instead of jumping, so
//
//   masterUs() = local microseconds + offset
//
// reads the same instant on every node (within a few ms on Wi-Fi). A node
// that becomes master keeps its offset, so the shared timeline continues.
//
// Cues (LAUNCH / PAUSE / RESUME / SEEK / STOP) carry master-clock times and
// go unicast to every peer plus one broadcast, three times; receivers drop
// repeats by (sender, seq). They are not multicast: Wi-Fi multicast has no
// ACK and waits for DTIM.
//
// Wire (little-endian): 20-byte header
//   0 "WHP3"  4 op  5 flags  6 seq u16  8 sender u32  12 group u32  16 cue u32
// then per op:
//   HELLO   role u8, api u8, hflags u8, rsv u8, master u32, rttUs u32,
//           waitGroup u32, name[24]; + if hasCue: createdAt i64, startAt i64,
//           startPos u32, dur u32, pausePos u32
//   PING    t1 i64                     (t1 local us of the sender)
//   PONG    t1 i64, t2 i64, t3 i64     (t2/t3 master us at rx/tx)
//   LAUNCH  createdAt i64, startAt i64, startPos u32, dur u32
//   PAUSE   at i64, pos u32
//   RESUME  startAt i64, startPos u32
//   SEEK    startAt i64, startPos u32  (flags.paused keeps it paused)
//   STOP    at i64
// Header flags: bit0 loop, bit1 paused. HELLO hflags: bit0 synced,
// bit1 clock master, bit2 has cue, bit3 cue paused, bit4 cue loop.

static constexpr uint16_t kSyncPort = 4777;
static constexpr uint8_t kSyncApi = 2;

enum class SyncRole : uint8_t { Node = 1, Companion = 2, Host = 3 };

static constexpr uint8_t kSyncOpHello = 1;
static constexpr uint8_t kSyncOpPing = 2;
static constexpr uint8_t kSyncOpPong = 3;
static constexpr uint8_t kSyncOpLaunch = 4;
static constexpr uint8_t kSyncOpPause = 5;
static constexpr uint8_t kSyncOpResume = 6;
static constexpr uint8_t kSyncOpSeek = 7;
static constexpr uint8_t kSyncOpStop = 8;
// Not on the wire: a peer's HELLO reported a running cue.
static constexpr uint8_t kSyncOpState = 100;

struct CueMsg {
  uint8_t op;
  bool loop;
  bool paused;
  uint32_t sender;
  uint32_t group;
  uint32_t cue;
  int64_t createdAt;
  int64_t startAt;
  int64_t at;
  uint32_t startPos;
  uint32_t dur;
  uint32_t pos;
};

struct CueSummary {
  uint32_t group;
  uint32_t cue;
  int64_t createdAt;
  int64_t startAt;
  uint32_t startPos;
  uint32_t dur;
  uint32_t pausePos;
  bool loop;
  bool paused;
};

struct SyncPeer {
  uint32_t id;
  IPAddress ip;
  char name[24];
  uint8_t role;
  uint8_t api;
  uint32_t master;
  uint32_t rttUs;
  uint32_t waitGroup;
  uint32_t lastMs;
  uint32_t masterForMs;
  bool synced;
  bool isMaster;
  bool hasCue;
  CueSummary cue;
};

class SyncNet {
public:
  static void begin();
  static void service();

  static uint32_t selfId();
  static int64_t localUs();
  static int64_t masterUs();
  static bool isClockMaster();
  static uint32_t clockMasterId();
  static bool synced();
  static uint32_t rttUs();
  // Sum of clock steps (> 30 ms, e.g. a new master's timeline) since the
  // last call; running schedules add it to stay continuous.
  static int64_t takeClockStep();

  static void setRole(SyncRole role);
  static SyncRole role();
  static void setHelloState(const CueSummary *cue, uint32_t waitGroup);
  static void helloSoon();

  static bool pollCue(CueMsg &out);
  static void sendCue(const CueMsg &m);

  static uint8_t peerCount();
  static const SyncPeer *peerAt(uint8_t i);
  // This node has the lowest id among alive nodes waiting on group.
  static bool lowestWaiting(uint32_t group);
};
