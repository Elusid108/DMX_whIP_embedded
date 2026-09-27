#pragma once

#include <stdint.h>

// Live ArtSync / E1.31 fence, and group playback on the cue bus v3
// (sync_net.h: shared clock, roster, cues).
//
// Live: Art-Net ArtSync (opcode 0x5200 on UDP 6454) and E1.31 synchronization
// PDUs raise a fence; the render path releases the latest live frame on it.
// Sync-only packets must not call LiveInput::push. No sync for ~2 s: portal
// FPS + buf 0-3 as before.
//
// Group playback: a .dmx whose sidecar names a sync group is one slice of a
// show split across nodes. Play on any node that holds a slice launches the
// whole group: LAUNCH says "group G, position P, starts at network time T".
// Every member binds its slice, seeks, prebuffers, and starts at T on the
// shared clock; nobody streams ticks. Pause / Resume / Stop on any member
// apply to the group at a network time. A member that boots or joins late
// picks the running cue out of a peer's HELLO and seeks to it.
//
// Loop: a cue launched from a file that loops on itself wraps every dur ms
// on every member. Otherwise the launcher's playlist advances and launches
// the next grouped file; followers return to their own show (takeover
// snapshot) if no new cue arrives shortly after the end.
//
// Sync takeover Yes lets a foreign group borrow a node that is playing its
// own show; No only joins when idle. Uni-Sync gates copied ("uni") clips.

static constexpr uint32_t kLiveSyncHoldMs = 2000;

class Sync {
public:
  static void begin();
  static void service();

  static void onArtSync();
  static void onE131Sync(uint16_t universe);
  static bool liveSyncActive();
  static bool hasLiveFence();
  static bool takeLiveFence();

  // Playback bound a file (any task; queued onto the loop task).
  static void onPlayFile(const char *path);
  // Our own playlist moved on to the next file (any task).
  static void notePlaylistAdvance();

  // Local transport (HTTP / portal / companion). A bound grouped file
  // launches the group; inside a cue these act on the whole group.
  static void noteLocalTrigger();
  static bool noteLocalPause();
  static bool noteLocalResume();
  static void noteStopped();
  // The Stream button: leave the cue here so the live stream owns the LEDs.
  static void releaseToLive();
  // The next bound file plays on this node alone, even if it is a grouped
  // slice (Stream mode plays the full show here and feeds the others live).
  static void playUngrouped();

  // Following a group schedule (the render path uses Playback's schedule).
  static bool inCue();
  // Bound to a grouped file, waiting for a launch (boot / autoplay).
  static bool waitingForCue();
  // This node launched the running cue.
  static bool isLauncher();
  static bool cuePaused();

  static bool hasGroup();
  static const char *groupId();
  static uint8_t memberCount();
  static const char *memberNameAt(uint8_t i);
};
