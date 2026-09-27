#include "sync.h"

#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "sync_net.h"

#include <Arduino.h>
#include <SD.h>
#include <cstdlib>
#include <cstring>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern TaskHandle_t loopTaskHandle;

static void onPlayFileNow(const char *path);
static void playlistAdvanceNow();

namespace {

// Members start this long after a launch so every node can bind, seek and
// prebuffer its slice first.
static constexpr uint32_t kLaunchLeadMs = 300;
static constexpr uint32_t kPauseLeadMs = 120;
static constexpr uint32_t kResumeLeadMs = 200;
// A grouped slice bound by autoplay / boot waits this long for a running cue
// before the lowest-id waiting node launches it.
static constexpr uint32_t kWaitLaunchMs = 2500;
// After a non-looping cue ends, wait this long for the launcher's next cue
// before a follower returns to its own show.
static constexpr uint32_t kEndGraceMs = 1500;
static constexpr uint32_t kLagSeekMs = 1000;

static constexpr uint8_t kSyncMaxMembers = 8;
static constexpr uint32_t kSyncNameLen = 64;
static constexpr uint32_t kSyncMacLen = 18;

// ---------------------------------------------------------------- live fence

static uint32_t s_liveSyncMs = 0;
static bool s_liveFence = false;
static bool s_liveActive = false;
static bool s_loggedArtSync = false;
static bool s_loggedE131 = false;

// ---------------------------------------------------------------- bound file

static char s_boundPath[kSdPathLen];
static char s_group[kSdGroupLen];
static bool s_groupUni = false;
static uint32_t s_groupHash = 0;
static uint32_t s_groupDur = 0;
static uint8_t s_memberN = 0;
static char s_memberName[kSyncMaxMembers][kSyncNameLen];
static char s_memberMac[kSyncMaxMembers][kSyncMacLen];

// ---------------------------------------------------------------- cue

struct Cue {
  bool active;
  bool bound;
  bool loop;
  bool paused;
  uint32_t id;
  uint32_t group;
  uint32_t launcher;
  int64_t createdAt;
  int64_t startAt;
  uint32_t startPos;
  uint32_t dur;
  uint32_t pausePos;
  char path[kSdPathLen];
};
static Cue s_cue = {};

struct Pending {
  bool on;
  uint8_t op;
  int64_t at;
  uint32_t pos;
};
static Pending s_pend = {};

static bool s_launchArmed = false;
static bool s_advanceArmed = false;
static uint32_t s_waitGroup = 0;
static uint32_t s_waitSince = 0;
static bool s_stopSticky = false;
static bool s_liveRelease = false;
static bool s_restoring = false;
static uint32_t s_endMs = 0;

// The show this node was on before a group borrowed it (RAM only).
struct ShowSnap {
  bool valid;
  bool hadFile;
  bool hold;
  bool parked;
  bool waiting;
  PlaySrc src;
  PlayFileLoop fileLoop;
  PlayFolderRep folderRep;
  uint8_t n;
  uint32_t t_ms;
  char path[kSdPathLen];
};
static ShowSnap s_snap = {};

// ---------------------------------------------------------------- sidecar

static bool sidecarPath(const char *dmx, char *out, size_t n) {
  if (!dmx || !out || n < 8) {
    return false;
  }
  const size_t len = strlen(dmx);
  if (len < 5 || len + 2 > n) {
    return false;
  }
  snprintf(out, n, "%s", dmx);
  memcpy(out + len - 4, ".json", 6);
  return true;
}

static bool extractJsonString(const char *buf, const char *key, char *out,
                              size_t outLen) {
  if (!buf || !key || !out || outLen < 2) {
    return false;
  }
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = strstr(buf, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (*p != ':') {
    return false;
  }
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (*p != '"') {
    return false;
  }
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outLen) {
    if (*p == '\\' && p[1]) {
      p++;
    }
    out[i++] = *p++;
  }
  out[i] = '\0';
  return i > 0;
}

static uint32_t extractJsonUint(const char *buf, const char *key) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = buf ? strstr(buf, needle) : nullptr;
  if (!p) {
    return 0;
  }
  p = strchr(p + strlen(needle), ':');
  return p ? static_cast<uint32_t>(strtoul(p + 1, nullptr, 10)) : 0;
}

static void parseMembers(const char *buf) {
  s_memberN = 0;
  const char *p = buf ? strstr(buf, "\"members\"") : nullptr;
  p = p ? strchr(p, '[') : nullptr;
  if (!p) {
    return;
  }
  p++;
  while (*p && s_memberN < kSyncMaxMembers) {
    const char *obj = strchr(p, '{');
    const char *end = obj ? strchr(obj, '}') : nullptr;
    if (!obj || !end) {
      break;
    }
    char tmp[192];
    const size_t n = static_cast<size_t>(end - obj + 1);
    if (n < sizeof(tmp)) {
      memcpy(tmp, obj, n);
      tmp[n] = '\0';
      extractJsonString(tmp, "n", s_memberName[s_memberN], kSyncNameLen);
      extractJsonString(tmp, "m", s_memberMac[s_memberN], kSyncMacLen);
      if (s_memberName[s_memberN][0]) {
        s_memberN += 1;
      }
    }
    p = end + 1;
  }
}

static void loadBoundGroup(const char *path) {
  s_group[0] = '\0';
  s_groupHash = 0;
  s_groupUni = false;
  s_groupDur = 0;
  s_memberN = 0;
  char side[kSdPathLen];
  if (!path || !path[0] || !sidecarPath(path, side, sizeof(side))) {
    return;
  }
  char buf[768];
  int n = 0;
  if (SdInfo::lock(1000)) {
    File f = SD.exists(side) ? SD.open(side, FILE_READ) : File();
    if (f) {
      n = f.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf) - 1);
      f.close();
    }
    SdInfo::unlock();
  }
  if (n <= 0) {
    return;
  }
  buf[n] = '\0';
  if (!extractJsonString(buf, "group", s_group, sizeof(s_group))) {
    s_group[0] = '\0';
    return;
  }
  parseMembers(buf);
  char kind[8] = {};
  if (extractJsonString(buf, "kind", kind, sizeof(kind))) {
    s_groupUni = strcmp(kind, "uni") == 0;
  }
  s_groupDur = extractJsonUint(buf, "dur");
  s_groupHash = SdInfo::hashGroup(s_group);
}

// The bound file is a slice this node plays in a group.
static bool boundGrouped() {
  return s_group[0] && s_memberN >= 2 && !(s_groupUni && !LiveCfg::uniSync());
}

// ---------------------------------------------------------------- snapshot

static void clearSnap() { s_snap.valid = false; }

static void captureSnap() {
  if (s_snap.valid) {
    return;
  }
  s_snap.src = PlayCfg::src();
  s_snap.fileLoop = PlayCfg::fileLoop();
  s_snap.folderRep = PlayCfg::folderRep();
  s_snap.n = PlayCfg::folderN();
  snprintf(s_snap.path, sizeof(s_snap.path), "%s", PlayCfg::path());
  s_snap.hold = Playback::hold();
  s_snap.parked = Playback::parked();
  s_snap.hadFile = Playback::hasFile() && !Playback::parked();
  s_snap.waiting = s_waitGroup != 0;
  s_snap.t_ms = Playback::tUs() / 1000u;
  s_snap.valid = true;
  LOG_V("sync", "borrowed from %s t_ms=%u", s_snap.path,
        static_cast<unsigned>(s_snap.t_ms));
}

static void restoreSnap() {
  if (!s_snap.valid) {
    return;
  }
  const ShowSnap snap = s_snap;
  clearSnap();
  // A node that was waiting for a group goes back to waiting; any other
  // show resumes locally without re-launching its group.
  s_restoring = !snap.waiting;
  LOG_V("sync", "return to %s t_ms=%u", snap.path,
        static_cast<unsigned>(snap.t_ms));
  PlayCfg::set(snap.src, snap.path, snap.fileLoop, snap.folderRep, snap.n,
               false, false);
  Playback::setLoop(true);
  Playback::setHold(snap.hold);
  if (snap.parked) {
    s_restoring = false;
    Playback::park();
    return;
  }
  if (!snap.hadFile) {
    Playback::reload();
    Playback::play();
    return;
  }
  Playback::resumeAt(snap.t_ms);
}

// ---------------------------------------------------------------- cue state

static CueMsg cueMsg(uint8_t op) {
  CueMsg m = {};
  m.op = op;
  m.group = s_cue.group;
  m.cue = s_cue.id;
  m.loop = s_cue.loop;
  m.paused = s_cue.paused;
  return m;
}

static void leaveCue() {
  if (!s_cue.active) {
    return;
  }
  s_cue.active = false;
  s_pend.on = false;
  s_endMs = 0;
  Playback::clearSchedule();
  LOG_V("sync", "leave cue %08x", static_cast<unsigned>(s_cue.id));
}

// A follower's cue is over: back to the show it was on.
static void endCue() {
  const bool launcher = s_cue.launcher == SyncNet::selfId();
  leaveCue();
  Playback::setLoop(true);
  if (s_snap.valid) {
    restoreSnap();
  } else if (!launcher) {
    Playback::park();
  }
}

static void applySchedule() {
  Playback::setSchedule(s_cue.startPos, s_cue.startAt, s_cue.dur, s_cue.loop);
  if (s_cue.paused) {
    Playback::pauseSchedule(s_cue.pausePos);
  }
}

static void launchLocal() {
  if (!boundGrouped()) {
    return;
  }
  const int64_t now = SyncNet::masterUs();
  s_cue = {};
  s_cue.active = true;
  s_cue.bound = true;
  s_cue.id = esp_random() | 1u;
  s_cue.group = s_groupHash;
  s_cue.launcher = SyncNet::selfId();
  s_cue.createdAt = now;
  s_cue.startAt = now + static_cast<int64_t>(kLaunchLeadMs) * 1000;
  s_cue.startPos = 0;
  s_cue.dur = s_groupDur ? s_groupDur : Playback::durationMs();
  s_cue.loop = Playback::loopsOnItself();
  snprintf(s_cue.path, sizeof(s_cue.path), "%s", s_boundPath);
  s_waitGroup = 0;
  s_stopSticky = false;
  s_endMs = 0;
  applySchedule();
  Playback::seekSchedule(0);
  Playback::play();
  CueMsg m = cueMsg(kSyncOpLaunch);
  m.createdAt = s_cue.createdAt;
  m.startAt = s_cue.startAt;
  m.startPos = s_cue.startPos;
  m.dur = s_cue.dur;
  SyncNet::sendCue(m);
  LOG_V("sync", "launch %s cue=%08x dur=%u loop=%u", s_group,
        static_cast<unsigned>(s_cue.id), static_cast<unsigned>(s_cue.dur),
        s_cue.loop ? 1u : 0u);
}

static void join(const CueMsg &m, int idx) {
  const char *path = SdInfo::fileAt(static_cast<uint8_t>(idx));
  if (!s_cue.active) {
    captureSnap();
  }
  s_cue = {};
  s_cue.active = true;
  s_cue.id = m.cue;
  s_cue.group = m.group;
  s_cue.launcher = m.sender;
  s_cue.createdAt = m.createdAt;
  s_cue.startAt = m.startAt;
  s_cue.startPos = m.startPos;
  s_cue.dur = m.dur;
  s_cue.loop = m.loop;
  s_cue.paused = m.paused;
  s_cue.pausePos = m.pos;
  snprintf(s_cue.path, sizeof(s_cue.path), "%s", path);
  s_waitGroup = 0;
  s_stopSticky = false;
  s_endMs = 0;
  s_pend.on = false;
  applySchedule();
  Playback::setLoop(s_cue.loop);
  Playback::setHold(false);
  if (Playback::hasFile() && !Playback::parked() &&
      strcmp(Playback::path(), path) == 0) {
    s_cue.bound = true;
    Playback::seekSchedule(0);
    Playback::play();
  } else {
    Playback::cueFile(path, 0, false);
    Playback::setHold(false);
  }
  LOG_V("sync", "join %s cue=%08x from %08x", path,
        static_cast<unsigned>(m.cue), static_cast<unsigned>(m.sender));
}

static void considerJoin(const CueMsg &m) {
  if (s_liveRelease || (LiveInput::active() && !Playback::hold())) {
    return;
  }
  if (s_cue.active && s_cue.id == m.cue) {
    // A peer's view of our own cue: pick up a pause / resume we missed.
    if (m.op == kSyncOpState && !s_pend.on &&
        (m.paused != s_cue.paused ||
         (!m.paused && m.startAt != s_cue.startAt))) {
      s_cue.paused = m.paused;
      s_cue.pausePos = m.pos;
      s_cue.startAt = m.startAt;
      s_cue.startPos = m.startPos;
      applySchedule();
      Playback::seekSchedule(0);
    }
    return;
  }
  if (s_cue.active && m.createdAt <= s_cue.createdAt) {
    return;
  }
  const int idx = SdInfo::findGroup(m.group);
  if (idx < 0) {
    return;
  }
  if (strcmp(SdInfo::markAt(static_cast<uint8_t>(idx)), "uni") == 0 &&
      !LiveCfg::uniSync()) {
    return;
  }
  if (!m.loop && m.dur && !m.paused) {
    const int64_t pos = static_cast<int64_t>(m.startPos) +
                        (SyncNet::masterUs() - m.startAt) / 1000;
    if (pos > static_cast<int64_t>(m.dur)) {
      return;
    }
  }
  const bool sameGroup = (s_cue.active && s_cue.group == m.group) ||
                         s_waitGroup == m.group || s_groupHash == m.group;
  const bool idle = !Playback::hasFile() || Playback::parked() ||
                    !Playback::playing() || s_waitGroup != 0;
  if (s_stopSticky && (sameGroup || !LiveCfg::takeover())) {
    return;
  }
  if (!sameGroup && !idle && !LiveCfg::takeover()) {
    return;
  }
  join(m, idx);
}

static void handleCue(const CueMsg &m) {
  switch (m.op) {
  case kSyncOpLaunch:
  case kSyncOpState:
    considerJoin(m);
    return;
  default:
    break;
  }
  if (!s_cue.active || m.group != s_cue.group) {
    return;
  }
  switch (m.op) {
  case kSyncOpPause:
    s_pend = {true, kSyncOpPause, m.at, m.pos};
    break;
  case kSyncOpResume:
  case kSyncOpSeek:
    s_pend.on = false;
    s_cue.startAt = m.startAt;
    s_cue.startPos = m.startPos;
    s_cue.paused = m.op == kSyncOpSeek && m.paused;
    s_cue.pausePos = m.startPos;
    applySchedule();
    if (m.op == kSyncOpSeek) {
      Playback::seekSchedule(0);
    }
    break;
  case kSyncOpStop:
    s_pend = {true, kSyncOpStop, m.at, 0};
    break;
  default:
    break;
  }
}

static void applyPending() {
  if (!s_pend.on || SyncNet::masterUs() < s_pend.at) {
    return;
  }
  const Pending p = s_pend;
  s_pend.on = false;
  if (p.op == kSyncOpPause) {
    s_cue.paused = true;
    s_cue.pausePos = p.pos;
    Playback::pauseSchedule(p.pos);
    LOG_V("sync", "group pause %u", static_cast<unsigned>(p.pos));
    return;
  }
  if (p.op == kSyncOpStop) {
    LOG_V("sync", "group stop");
    if (s_snap.valid) {
      endCue();
      return;
    }
    leaveCue();
    Playback::setLoop(true);
    Playback::park();
  }
}

static void publishHello() {
  if (!s_cue.active) {
    SyncNet::setHelloState(nullptr, s_waitGroup);
    return;
  }
  CueSummary c = {};
  c.group = s_cue.group;
  c.cue = s_cue.id;
  c.createdAt = s_cue.createdAt;
  c.startAt = s_cue.startAt;
  c.startPos = s_cue.startPos;
  c.dur = s_cue.dur;
  c.pausePos = s_cue.pausePos;
  c.loop = s_cue.loop;
  c.paused = s_cue.paused;
  SyncNet::setHelloState(&c, 0);
}

// ---------------------------------------------------------------- task queue

// The play task binds files and advances playlists; Sync state and the cue
// socket belong to the Arduino loop task, so those calls are queued.
enum class EvKind : uint8_t { PlayFile = 1, Advance = 2 };
struct SyncEvt {
  EvKind kind;
  char path[kSdPathLen];
};
static constexpr UBaseType_t kEvDepth = 6;
static QueueHandle_t s_evq = nullptr;

static bool onLoopTask() {
  return loopTaskHandle == nullptr ||
         xTaskGetCurrentTaskHandle() == loopTaskHandle;
}

static void postEvent(EvKind kind, const char *path) {
  if (s_evq == nullptr) {
    s_evq = xQueueCreate(kEvDepth, sizeof(SyncEvt));
    if (s_evq == nullptr) {
      return;
    }
  }
  SyncEvt e;
  e.kind = kind;
  snprintf(e.path, sizeof(e.path), "%s", path ? path : "");
  if (xQueueSend(s_evq, &e, pdMS_TO_TICKS(50)) != pdTRUE) {
    LOG_C("sync", "event queue full");
  }
}

static void drainEvents() {
  if (s_evq == nullptr) {
    return;
  }
  SyncEvt e;
  while (xQueueReceive(s_evq, &e, 0) == pdTRUE) {
    if (e.kind == EvKind::PlayFile) {
      onPlayFileNow(e.path);
    } else {
      playlistAdvanceNow();
    }
  }
}

} // namespace

static void onPlayFileNow(const char *path) {
  const bool restoring = s_restoring && path && path[0];
  if (path && path[0]) {
    s_restoring = false;
  }
  snprintf(s_boundPath, sizeof(s_boundPath), "%s", path ? path : "");
  loadBoundGroup(path);
  if (!path || !path[0]) {
    return;
  }
  if (s_cue.active) {
    if (strcmp(path, s_cue.path) == 0 && !s_launchArmed) {
      s_cue.bound = true;
      if (s_cue.dur == 0) {
        // Launched without a length (companion): use this slice's.
        s_cue.dur = s_groupDur ? s_groupDur : Playback::durationMs();
        applySchedule();
      }
      Playback::seekSchedule(0);
      Playback::play();
      return;
    }
    // A local Play or our own playlist moved to another file.
    leaveCue();
  }
  if (!boundGrouped() || restoring) {
    s_waitGroup = 0;
    s_launchArmed = false;
    s_advanceArmed = false;
    return;
  }
  if (s_launchArmed || s_advanceArmed) {
    s_launchArmed = false;
    s_advanceArmed = false;
    launchLocal();
    return;
  }
  // Autoplay / boot bound a slice: wait for the group's cue.
  s_waitGroup = s_groupHash;
  s_waitSince = millis();
  LOG_V("sync", "wait for group %s", s_group);
}

static void playlistAdvanceNow() {
  // Our own playlist reached this file: a grouped one conducts the group.
  if (s_cue.active && s_cue.launcher != SyncNet::selfId()) {
    return;
  }
  s_advanceArmed = true;
}

void Sync::begin() { SyncNet::begin(); }

void Sync::service() {
  drainEvents();
  SyncNet::service();

  const uint32_t now = millis();
  if (s_liveActive &&
      (s_liveSyncMs == 0 || now - s_liveSyncMs >= kLiveSyncHoldMs)) {
    s_liveActive = false;
    s_liveFence = false;
    LOG_V("sync", "live free-run (no ArtSync/E1.31)");
  }
  if (s_liveRelease && !LiveInput::active()) {
    s_liveRelease = false;
  }

  const int64_t step = SyncNet::takeClockStep();
  if (step != 0 && s_cue.active) {
    s_cue.startAt += step;
    Playback::rebaseSchedule(step);
    if (s_pend.on) {
      s_pend.at += step;
    }
  }

  CueMsg m;
  while (SyncNet::pollCue(m)) {
    handleCue(m);
  }
  applyPending();

  if (s_cue.active && s_cue.bound) {
    if (Playback::scheduleLagMs() > kLagSeekMs) {
      Playback::seekSchedule(80);
    }
    if (Playback::scheduleEnded() ||
        (!Playback::hasFile() && !Playback::reloadPending())) {
      if (s_endMs == 0) {
        s_endMs = now;
      } else if (now - s_endMs >= kEndGraceMs) {
        endCue();
      }
    } else {
      s_endMs = 0;
    }
  }

  if (s_waitGroup != 0 && !s_cue.active && s_groupHash == s_waitGroup &&
      now - s_waitSince >= kWaitLaunchMs &&
      SyncNet::lowestWaiting(s_waitGroup)) {
    launchLocal();
  }
  publishHello();
}

void Sync::onArtSync() {
  s_liveSyncMs = millis();
  s_liveFence = true;
  if (!s_liveActive) {
    s_liveActive = true;
    LOG_V("sync", "live fence ArtSync");
  }
  if (!s_loggedArtSync) {
    s_loggedArtSync = true;
    LOG_V("sync", "ArtSync");
  }
}

void Sync::onE131Sync(uint16_t universe) {
  s_liveSyncMs = millis();
  s_liveFence = true;
  if (!s_liveActive) {
    s_liveActive = true;
    LOG_V("sync", "live fence E1.31 uni=%u", universe);
  }
  if (!s_loggedE131) {
    s_loggedE131 = true;
    LOG_V("sync", "E1.31 sync uni=%u", universe);
  }
}

bool Sync::liveSyncActive() { return s_liveActive; }

bool Sync::hasLiveFence() { return s_liveActive && s_liveFence; }

bool Sync::takeLiveFence() {
  if (!s_liveFence) {
    return false;
  }
  s_liveFence = false;
  return true;
}

void Sync::onPlayFile(const char *path) {
  if (!onLoopTask()) {
    postEvent(EvKind::PlayFile, path);
    return;
  }
  drainEvents();
  onPlayFileNow(path);
}

void Sync::notePlaylistAdvance() {
  if (!onLoopTask()) {
    postEvent(EvKind::Advance, nullptr);
    return;
  }
  drainEvents();
  playlistAdvanceNow();
}

void Sync::noteLocalTrigger() {
  clearSnap();
  s_stopSticky = false;
  s_liveRelease = false;
  s_launchArmed = true;
}

bool Sync::noteLocalPause() {
  if (!s_cue.active || s_cue.paused) {
    return s_cue.active;
  }
  const int64_t at = SyncNet::masterUs() + static_cast<int64_t>(kPauseLeadMs) * 1000;
  const uint32_t pos = Playback::scheduleTotalMs() + kPauseLeadMs;
  s_pend = {true, kSyncOpPause, at, pos};
  CueMsg m = cueMsg(kSyncOpPause);
  m.at = at;
  m.pos = pos;
  SyncNet::sendCue(m);
  return true;
}

bool Sync::noteLocalResume() {
  if (!s_cue.active || !s_cue.paused) {
    return false;
  }
  s_pend.on = false;
  s_cue.paused = false;
  s_cue.startPos = s_cue.pausePos;
  s_cue.startAt = SyncNet::masterUs() + static_cast<int64_t>(kResumeLeadMs) * 1000;
  applySchedule();
  CueMsg m = cueMsg(kSyncOpResume);
  m.startAt = s_cue.startAt;
  m.startPos = s_cue.startPos;
  SyncNet::sendCue(m);
  return true;
}

void Sync::noteStopped() {
  if (s_cue.active) {
    CueMsg m = cueMsg(kSyncOpStop);
    m.at = SyncNet::masterUs();
    SyncNet::sendCue(m);
    leaveCue();
  }
  clearSnap();
  s_stopSticky = true;
  s_waitGroup = 0;
  s_launchArmed = false;
  Playback::setLoop(true);
}

void Sync::releaseToLive() {
  leaveCue();
  clearSnap();
  s_liveRelease = true;
  s_waitGroup = 0;
  Playback::setLoop(true);
  LOG_V("sync", "release to live");
}

void Sync::playUngrouped() {
  leaveCue();
  clearSnap();
  s_waitGroup = 0;
  s_launchArmed = false;
  s_stopSticky = false;
  s_restoring = true;
}

bool Sync::inCue() { return s_cue.active; }

bool Sync::waitingForCue() { return s_waitGroup != 0 && !s_cue.active; }

bool Sync::isLauncher() {
  return s_cue.active && s_cue.launcher == SyncNet::selfId();
}

bool Sync::cuePaused() { return s_cue.active && s_cue.paused; }

bool Sync::hasGroup() { return s_group[0] != '\0'; }

const char *Sync::groupId() { return s_group; }

uint8_t Sync::memberCount() { return s_memberN; }

const char *Sync::memberNameAt(uint8_t i) {
  return i < s_memberN ? s_memberName[i] : "";
}
