// RP2040 / RP2350 standalone player: the classes left out of the build
// (platformio.ini [rp] build_src_filter) answer here as "nothing on": no live
// input, no group sync, no network, no console fixture. The kept code (main
// loop, playback, SD, serial commands, button) then runs its standalone path.
#include "platform.h"

#if !WHIP_HAS_NET

#include <Arduino.h>

#include "pico/time.h"

#include "fixture.h"
#include "live_input.h"
#include "ota.h"
#include "sync.h"
#include "sync_net.h"
#include "wifi_setup.h"

// Live Art-Net / sACN input.
void LiveInput::begin() {}
void LiveInput::service() {}
void LiveInput::applyCfg() {}
bool LiveInput::active() { return false; }
bool LiveInput::renderLeds(bool) { return false; }

// Cue-bus group sync: every play is ungrouped; pause / resume are local.
void Sync::begin() {}
void Sync::service() {}
bool Sync::liveSyncActive() { return false; }
bool Sync::hasLiveFence() { return false; }
bool Sync::takeLiveFence() { return false; }
void Sync::onPlayFile(const char *) {}
void Sync::notePlaylistAdvance() {}
bool Sync::noteLocalPause() { return false; }
bool Sync::noteLocalResume() { return false; }
bool Sync::inCue() { return false; }
bool Sync::waitingForCue() { return false; }

// The shared show clock is just this board's clock.
int64_t SyncNet::masterUs() { return static_cast<int64_t>(time_us_64()); }

void WifiSetup::begin() {}
void WifiSetup::service() {}

void Ota::bootGuard() {}

// Console fixture (driven by a DMX console over the network): never active.
void Fixture::begin() {}
void Fixture::service() {}
FixDrive Fixture::beginFrame(uint32_t) { return FixDrive::None; }
bool Fixture::animating() { return false; }
void Fixture::applyLook(uint16_t, uint8_t *, uint8_t) {}
bool Fixture::locating() { return false; }
void Fixture::cancelLocate() {}
void Fixture::renderLocate() {}

#endif
