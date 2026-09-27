#pragma once

#include <Arduino.h>
#include <stdint.h>

// Advanced patch: the node as one console fixture.
//
// The fixture has its own patch (protocol, universe, channel) and a mode.
// Dim, RGB and Full start with a 13-channel header:
//   1 master dimmer, 2 strobe (0-9 open, 10-255 = 1-25 Hz), 3 strobe colour
//   (0 white, 1-255 round the colour wheel), 4 strobe intensity (what the
//   strobe's off phase shows: that colour at this level; 0 = blackout),
//   5 hue shift, 6-8 colour filter R/G/B (amount removed, 0 = none), 9-11
//   colour add R/G/B, 12 folder (0 = SD root, n = n-th folder A-Z), 13 clip
//   (0 = normal playback: back to the startup playlist after a pick; n =
//   the n-th look in that folder, A-Z).
// Basic is 5 channels and nothing else: 1 intensity, 2 strobe, 3 hue shift,
//   4 folder, 5 clip; it overlays the look like Dim, with no sub-fixtures.
// 0 on every channel is "no effect", so channels the console does not patch
// (sent as 0) are released. Dimmers (master and each sub-fixture's) are the
// exception 0 can't cover: each stays open until the console first sends it
// above 0, then works as a normal dimmer until the fixture universe is lost.
// Then, per mode:
//   Basic: nothing more (sub-fixtures are kept but not used).
//   Dim  : 2 ch per sub-fixture (dim, strobe); colour = the recorded look
//          (SD playback or the live stream on the main patch). The fixture
//          universe is control only and never takes over playback.
//   Rgb  : 5 ch per sub-fixture (dim, strobe, R, G, B); colour = console.
//          Pixels outside every sub-fixture stay black.
//   Full : channels-per-pixel for every LED in map order, never split across
//          a universe, continuing into the next universes; colour = console.
//
// Pixels are the main patch's LEDs in map order (output -> segment ->
// pixel), the same global index LedBus uses. A pixel belongs to at most one
// sub-fixture. Strobes run on the cue-bus clock, so they flash together on
// every node.
//
// Stored in LittleFS on the "spiffs" partition (/fixture.bin; pixel names in
// /pxnames.txt), so it survives an SD swap.

enum class FixMode : uint8_t { Dim = 0, Rgb = 1, Full = 2, Basic = 3 };
enum class FixDrive : uint8_t { None = 0, Look = 1, Console = 2 };

static constexpr uint8_t kFixHeader = 13;      // Dim, RGB, Full
static constexpr uint8_t kFixHeaderBasic = 5;
static constexpr uint8_t kFixMaxSubs = 96;
static constexpr uint16_t kFixMaxRanges = 256;
static constexpr uint8_t kFixNameLen = 24;

class Fixture {
public:
  static void begin();
  // Rebuilds after a main-patch change; acts on clip select.
  static void service();

  // ---- intake (the receivers and LiveInput ask these)
  static bool wantsArtNet(uint16_t uni);
  static bool wantsSacn(uint16_t uni);
  static bool anyArtNet();
  static bool anySacn();
  static uint8_t collectSacnUniverses(uint16_t *out, uint8_t max);
  // A universe of this fixture's patch (enabled and valid only).
  static bool isFixtureUniverse(bool sacn, uint16_t uni);
  // Dim / Basic: fixture universes carry control, not content.
  static bool controlOnly();

  // ---- render
  // Once per rendered frame: reads the header and sub-fixture channels.
  static FixDrive beginFrame(uint32_t nowMs);
  // The fixture changes pixels between content frames (strobe, dimmer
  // moves): render every show tick, not only when content changes.
  static bool animating();
  // Dim: transform the look's bytes for global pixel g in place.
  static void applyLook(uint16_t g, uint8_t *px, uint8_t cpp);
  // Rgb / Full: write global pixel g's bytes from the console.
  static void consolePixel(uint16_t g, uint8_t *px, uint8_t cpp);

  // ---- config (HTTP)
  static void stageBegin(bool en, FixMode mode, bool sacn, uint16_t uni, uint16_t ch);
  static bool stageSub(const char *name, const char *ranges, const char *&error);
  static bool stageCommit(const char *&error);
  static bool parseMode(const char *s, FixMode &out);
  static const char *modeName(FixMode mode);

  static void appendJson(String &out);      // GET /fixture body
  static void appendStatus(String &out);    // "fixture":{...}
  static void appendNames(String &out, uint16_t from, uint16_t n);
  static bool setNames(uint16_t from, const String &lines, const char *&error);

  // Locate: light the given pixels ("0-11,40") white, the rest dark, for ms
  // (max 15 s; 0 cancels). Live input cancels it.
  static bool locate(const char *ranges, uint32_t ms, const char *&error);
  static bool locating();
  static void cancelLocate();
  static void renderLocate();
};
