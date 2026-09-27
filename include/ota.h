#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Over-the-air firmware updates.
//
// POST /ota (multipart, file part "firmware"): the image streams into the
// idle app slot. It must carry this build's tag (WHIPFW:<board>:<ver>:<api>;)
// for the same board. On success the node reboots into it. Refused while the
// node is busy (playing a show, live input, distributing, streaming) unless
// ?force=1.
//
// Rollback: a new image boots "pending". It is kept once the network and
// portal are up for 30 s. If it resets before then (crash loop) the
// bootloader boots the previous image; if it is still not healthy at 120 s it
// rolls itself back. /status "ota" reports rolled_back afterwards.
//
// Peer update: POST /ota/peers sends this node's own running image to every
// same-board node on the cue bus running an older version, one at a time.
class Ota {
public:
  // Call first in setup(): notes a pending image, rolls back a crash loop.
  static void bootGuard();
  // Call from the loop with whether Wi-Fi (STA or AP) is up.
  static void service(bool netUp);

  // True while this node should not be interrupted.
  static bool busy();

  // Upload stream from the web server.
  static void uploadStart(int contentLength, bool force);
  static void uploadWrite(const uint8_t *data, size_t len);
  static void uploadEnd(bool aborted);
  // nullptr when the last upload was written and verified.
  static const char *uploadError();
  static const char *uploadVersion();
  static bool uploadOk();

  static bool startPeers(bool force, const char *&error);
  static bool peersRunning();

  // "ota":{...},"ota_peers":{...}
  static void appendStatus(String &out);
};
