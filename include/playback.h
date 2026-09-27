#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pixel_map.h"

// Playback engine: companion DMXREC (.dmx) → Matrix-scale ring. Does not
// drive LEDs. Console owns the fixture patch; this node copies its universe
// 1:1 onto the strip (N pixels × chips, GPIO from the board profile).
//
// Live is drop-to-latest. Playback is time-based: a frame shows when its
// DMXREC t_ms is due on the show clock. The clock anchors on the first frame
// after a start, seek, or underrun (pause on underrun, never invent frames),
// or on cue-bus ticks while following.
//
// FastLED.show() only in main. renderDue consumes the reader ring.

// Ring depth. PSRAM boards get the deep ring so SD latency never shows.
static constexpr uint8_t kPlayRingSlotsMin = 4;
static constexpr uint8_t kPlayRingSlotsPsram = 32;
static constexpr uint16_t kPlayMaxPixels = kLedCountMax;
static constexpr uint8_t kPlayMaxChips = kMaxChannelsPerPixel;
static constexpr uint16_t kPlayMaxPayload =
    static_cast<uint16_t>(kPlayMaxPixels * kPlayMaxChips);

class Playback {
public:
  static void begin();
  static void service();
  static void start();
  static void stop();
  static void park();
  static void play();
  static void pause();
  static void userPause();
  static void userResume();
  static void setLoop(bool on);
  static void reload();
  // Master heard a slave leave the show. This file loops: park. Otherwise next.
  static void releaseFromSync();

  // Playback hold: the file wins over a live stream until Stop, the Live
  // page Stream action, or the playlist has no next file.
  static bool hold();
  static void setHold(bool on);

  // Bind path when it is not the current file, then seek to show t_ms.
  // Does not take cue-bus master. savePlaylist false leaves NVS unchanged.
  static void cueFile(const char *path, uint32_t t_ms, bool savePlaylist = true);

  // Rebind the current playlist and seek to show t_ms.
  static void resumeAt(uint32_t t_ms);

  // True while a reload is waiting for the play task.
  static bool reloadPending();

  // Move an in-flight cue seek onto the newest show t_ms, or seek now.
  static void nudgeCue(uint32_t t_ms);

  // True when a tick should binary-seek. False when the reader is already
  // near t_ms or a seek is in progress. Play and Seek skip this and call
  // nudgeCue directly.
  static bool cueNeedsSeek(uint32_t t_ms);

  static bool hasFile();
  static bool parked();
  static bool userPaused();
  static bool running();
  static bool playing();
  static bool underrun();
  static uint8_t available();

  // Head-of-ring show timestamp (t_ms * 1000). False if empty (underrun).
  static bool peek(uint32_t &t_us);
  static bool peekFrame(uint32_t &t_us, uint32_t &frame);

  // True when the head frame is due on the show clock (or the clock is not
  // anchored yet and a frame is waiting).
  static bool frameDue(uint32_t nowMs);

  // Pop every frame due at nowMs and copy the newest into rgb. False when
  // nothing is due or on underrun (holds last output; no invent).
  static bool renderDue(uint8_t *rgb, size_t n, uint32_t nowMs);

  // Follower: a cue tick says the master's show clock read t_ms. Anchors the
  // local clock on the lowest-latency tick of a short window.
  static void syncTick(uint32_t t_ms, uint32_t nowMs);

  // Show position on the local clock (ms). Head frame time when unanchored.
  static uint32_t showPosMs(uint32_t nowMs);

  static bool seekMs(uint32_t t_ms);
  static bool seekFrame(uint32_t index);

  static uint32_t tUs();
  static uint32_t frameIndex();
  static uint16_t fps();
  static uint32_t payloadBytes();
  static const char *path();
};
