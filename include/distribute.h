#pragma once

#include <Arduino.h>
#include <stdint.h>

// Distribute: this node holds a full multi-universe show on its SD. For each
// peer on the cue bus it reads the peer's /status patch (output 0 window),
// slices the show to that window (last value of every universe carried
// forward; Art-Net <-> sACN universes renumbered to the peer's start), uploads
// the slice to the peer's /upload at the same path, then writes the same sync
// sidecar (group, members, length) on every member and on itself. After that
// the group plays exactly like a companion split push; this node plays its own
// window straight from the full file.
//
// Runs in its own task; progress is /status "dist".

// Stream: this node plays a full show and sends each peer the universes its
// patch uses, live (unicast Art-Net, or E1.31 to sACN-patched peers), paced
// to this node's own playback, with an ArtSync to each Art-Net peer after
// every frame. Members need no SD content: they play it as a live stream.
// Stops when this node stops playing the show, or on stop().
class StreamTx {
public:
  static bool start(const char *path, const char *&error);
  static void stop();
  static bool running();
  static void appendStatus(String &out);
};

class Distribute {
public:
  // Start distributing the .dmx at path. False (with a reason) if one is
  // already running or the path is not a playable show.
  static bool start(const char *path, const char *&error);
  static void appendStatus(String &out);
  static bool running();
};
