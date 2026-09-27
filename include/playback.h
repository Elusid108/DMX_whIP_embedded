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

  // The reader wraps this one file forever (a single-file playlist that
  // loops); a group cue launched from it loops too.
  static bool loopsOnItself();
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

  // Group schedule on the shared network clock (SyncNet::masterUs()):
  // position = startPos + (masterNow - startAt). loop wraps every dur ms
  // (0 = this file's own length). Survives rebinds until clearSchedule().
  static void setSchedule(uint32_t startPosMs, int64_t startAtMasterUs,
                          uint32_t durMs, bool loop);
  static void pauseSchedule(uint32_t posMs);
  static void clearSchedule();
  static bool scheduled();
  static bool schedulePaused();
  // Position since the file's first pass (not wrapped), ms.
  static uint32_t scheduleTotalMs();
  // A non-looping schedule has run past its length.
  static bool scheduleEnded();
  // The network clock stepped by deltaUs: keep the position continuous.
  static void rebaseSchedule(int64_t deltaUs);
  // Seek the reader to the scheduled position (plus leadMs).
  static void seekSchedule(uint32_t leadMs);
  // How far the scheduled position has run past the last shown frame while
  // the ring is empty (the reader fell behind), ms.
  static uint32_t scheduleLagMs();
  // Length of the bound file: last record time plus one frame, ms.
  static uint32_t durationMs();

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
