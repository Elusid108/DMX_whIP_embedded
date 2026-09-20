#include "sync.h"

#include "live_cfg.h"
#include "log.h"
#include "node_id.h"
#include "playback.h"

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

namespace {

static constexpr uint32_t kCueSnapUs = 80000;
static constexpr uint32_t kCueSnapMs = 80;
static constexpr uint32_t kCueJoinRetryMs = 2000;
static constexpr size_t kCueMaxPkt = 32;

static WiFiUDP s_udp;
static uint8_t s_pkt[kCueMaxPkt];
static bool s_begun = false;
static bool s_up = false;
static bool s_mcast = false;
static uint32_t s_joinMs = 0;
static bool s_loggedJoinFail = false;

static uint32_t s_liveSyncMs = 0;
static bool s_liveFence = false;
static bool s_liveActive = false;
static bool s_loggedArtSync = false;
static bool s_loggedE131 = false;

static bool s_follow = false;
static bool s_cuePlay = true;
static bool s_cuePulse = false;
static bool s_hasTime = false;
static bool s_hasFrame = false;
static uint32_t s_targetMs = 0;
static uint32_t s_targetFrame = 0;
static uint32_t s_cueMs = 0;
static bool s_loggedFollow = false;
static uint32_t s_loggedSeekMs = 0xFFFFFFFFu;
static uint32_t s_loggedSeekFrame = 0xFFFFFFFFu;
static uint32_t s_lastSnapMs = 0xFFFFFFFFu;
static uint32_t s_lastSnapFrame = 0xFFFFFFFFu;

static constexpr uint8_t kSyncMaxMembers = 8;
static constexpr uint32_t kSyncNameLen = 64;
static constexpr uint32_t kSyncMacLen = 18;
static constexpr uint32_t kSyncGroupLen = 40;

static char s_group[kSyncGroupLen];
static uint32_t s_groupHash = 0;
static uint8_t s_memberN = 0;
static char s_memberName[kSyncMaxMembers][kSyncNameLen];
static char s_memberMac[kSyncMaxMembers][kSyncMacLen];
static bool s_master = false;
static bool s_waitMaster = false;
static bool s_pendingLocal = false;
static uint32_t s_waitMs = 0;
static uint32_t s_lastEmitMs = 0;
static uint8_t s_lastEmitOp = 0;

static uint32_t hashGroup(const char *s) {
  uint32_t h = 2166136261u;
  if (!s) {
    return 0;
  }
  while (*s) {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}

static int nameCmp(const char *a, const char *b) {
  const char *pa = a ? a : "";
  const char *pb = b ? b : "";
  while (*pa && *pb) {
    if (*pa >= '0' && *pa <= '9' && *pb >= '0' && *pb <= '9') {
      long na = 0;
      long nb = 0;
      while (*pa >= '0' && *pa <= '9') {
        na = na * 10 + (*pa - '0');
        pa++;
      }
      while (*pb >= '0' && *pb <= '9') {
        nb = nb * 10 + (*pb - '0');
        pb++;
      }
      if (na != nb) {
        return na < nb ? -1 : 1;
      }
      continue;
    }
    char ca = *pa++;
    char cb = *pb++;
    if (ca >= 'A' && ca <= 'Z') {
      ca = static_cast<char>(ca + 32);
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = static_cast<char>(cb + 32);
    }
    if (ca != cb) {
      return ca < cb ? -1 : 1;
    }
  }
  if (*pa) {
    return 1;
  }
  if (*pb) {
    return -1;
  }
  return 0;
}

static bool macEq(const char *a, const char *b) {
  if (!a || !b || !a[0] || !b[0]) {
    return false;
  }
  size_t i = 0;
  size_t j = 0;
  while (a[i] && b[j]) {
    char ca = a[i++];
    char cb = b[j++];
    if (ca == ':' || ca == '-') {
      if (a[i]) {
        ca = a[i++];
      }
    }
    if (cb == ':' || cb == '-') {
      if (b[j]) {
        cb = b[j++];
      }
    }
    if (ca >= 'A' && ca <= 'Z') {
      ca = static_cast<char>(ca + 32);
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = static_cast<char>(cb + 32);
    }
    if (ca != cb) {
      return false;
    }
  }
  return a[i] == 0 && b[j] == 0;
}

static void localMac(char *out, size_t n) {
  if (!out || n < 18) {
    return;
  }
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static bool sidecarPath(const char *dmx, char *out, size_t n) {
  if (!dmx || !out || n < 8) {
    return false;
  }
  const size_t len = strlen(dmx);
  if (len < 5 || len + 2 > n) {
    return false;
  }
  snprintf(out, n, "%s", dmx);
  if (len < 4) {
    return false;
  }
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

static void parseMembers(const char *buf) {
  s_memberN = 0;
  if (!buf) {
    return;
  }
  const char *p = strstr(buf, "\"members\"");
  if (!p) {
    return;
  }
  p = strchr(p, '[');
  if (!p) {
    return;
  }
  p++;
  while (*p && s_memberN < kSyncMaxMembers) {
    const char *obj = strchr(p, '{');
    if (!obj) {
      break;
    }
    const char *end = strchr(obj, '}');
    if (!end) {
      break;
    }
    char tmp[192];
    const size_t n = static_cast<size_t>(end - obj + 1);
    if (n >= sizeof(tmp)) {
      p = end + 1;
      continue;
    }
    memcpy(tmp, obj, n);
    tmp[n] = '\0';
    extractJsonString(tmp, "n", s_memberName[s_memberN], kSyncNameLen);
    extractJsonString(tmp, "m", s_memberMac[s_memberN], kSyncMacLen);
    if (s_memberName[s_memberN][0]) {
      s_memberN += 1;
    }
    p = end + 1;
  }
}

static void clearGroup() {
  s_group[0] = '\0';
  s_groupHash = 0;
  s_memberN = 0;
  s_master = false;
  s_waitMaster = false;
}

static bool weAreElected() {
  if (s_memberN == 0) {
    return true;
  }
  char mac[kSyncMacLen];
  localMac(mac, sizeof(mac));
  const char *mine = NodeId::longName();
  int best = 0;
  for (uint8_t i = 1; i < s_memberN; ++i) {
    const int c = nameCmp(s_memberName[i], s_memberName[best]);
    if (c < 0 || (c == 0 && nameCmp(s_memberMac[i], s_memberMac[best]) < 0)) {
      best = i;
    }
  }
  if (nameCmp(s_memberName[best], mine) == 0) {
    if (!s_memberMac[best][0] || macEq(s_memberMac[best], mac)) {
      return true;
    }
  }
  return false;
}

static bool ownPacket(const IPAddress &from) {
  const IPAddress sta = WiFi.localIP();
  if (sta && from == sta) {
    return true;
  }
  return false;
}

static bool magicOk(const uint8_t *p) {
  return p[0] == static_cast<uint8_t>(kCueMagic0) &&
         p[1] == static_cast<uint8_t>(kCueMagic1) &&
         p[2] == static_cast<uint8_t>(kCueMagic2) &&
         p[3] == static_cast<uint8_t>(kCueMagic3);
}

static IPAddress cueGroup() {
  return IPAddress(kCueMcastA, kCueMcastB, kCueMcastC, kCueMcastD);
}

static bool bindUnicast() {
  if (!s_udp.begin(kCuePort)) {
    LOG_C("sync", "udp bind :%u failed", kCuePort);
    s_up = false;
    s_mcast = false;
    return false;
  }
  s_up = true;
  s_mcast = false;
  return true;
}

static bool joinMcast() {
  if (!s_up || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const uint32_t now = millis();
  if (s_joinMs != 0 && now - s_joinMs < kCueJoinRetryMs) {
    return false;
  }
  s_joinMs = now;
  const IPAddress group = cueGroup();
  s_udp.stop();
  if (!s_udp.beginMulticast(group, kCuePort)) {
    if (!s_loggedJoinFail) {
      s_loggedJoinFail = true;
      LOG_C("sync", "mcast join failed");
    }
    if (!bindUnicast()) {
      return false;
    }
    return false;
  }
  s_up = true;
  s_mcast = true;
  s_loggedJoinFail = false;
  LOG_V("sync", "cue %s:%u", group.toString().c_str(), kCuePort);
  return true;
}

static void enterFollow() {
  if (s_follow) {
    return;
  }
  s_follow = true;
  Playback::setLoop(false);
  if (!s_loggedFollow) {
    s_loggedFollow = true;
    LOG_V("sync", "cue follow (bus)");
  }
}

static void leaveFollow() {
  if (!s_follow) {
    return;
  }
  s_follow = false;
  s_cuePlay = true;
  s_hasTime = false;
  s_hasFrame = false;
  s_cuePulse = false;
  s_lastSnapMs = 0xFFFFFFFFu;
  s_lastSnapFrame = 0xFFFFFFFFu;
  Playback::setLoop(true);
  Playback::play();
  LOG_V("sync", "cue silent → local auto-play");
  s_loggedFollow = false;
}

static void applyPosition(uint8_t flags, uint32_t t_ms, uint32_t frame,
                          bool forceSeek) {
  s_hasTime = (flags & kCueFlagHasTime) != 0;
  s_hasFrame = (flags & kCueFlagHasFrame) != 0;
  if (s_hasTime) {
    s_targetMs = t_ms;
  }
  if (s_hasFrame) {
    s_targetFrame = frame;
  }
  if (!s_hasTime && !s_hasFrame) {
    return;
  }

  bool seek = forceSeek;
  const uint8_t queued = Playback::available();
  if (!seek && queued > 0) {
    uint32_t t_us = 0;
    uint32_t curFrame = 0;
    if (Playback::peekFrame(t_us, curFrame)) {
      if (s_hasTime) {
        const uint32_t target_us = s_targetMs * 1000u;
        if (t_us + kCueSnapUs < target_us || target_us + kCueSnapUs < t_us) {
          seek = true;
        }
      } else if (s_hasFrame) {
        const uint32_t a = curFrame;
        const uint32_t b = s_targetFrame;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > 4) {
          seek = true;
        }
      }
    }
  } else if (!seek && queued == 0) {
    if (s_hasTime) {
      if (s_lastSnapMs == 0xFFFFFFFFu) {
        seek = true;
      } else {
        const uint32_t a = s_targetMs;
        const uint32_t b = s_lastSnapMs;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > kCueSnapMs) {
          seek = true;
        }
      }
    } else if (s_hasFrame) {
      if (s_lastSnapFrame == 0xFFFFFFFFu) {
        seek = true;
      } else {
        const uint32_t a = s_targetFrame;
        const uint32_t b = s_lastSnapFrame;
        const uint32_t d = a > b ? a - b : b - a;
        if (d > 4) {
          seek = true;
        }
      }
    }
  }

  if (!seek) {
    return;
  }
  if (s_hasFrame && !s_hasTime) {
    Playback::seekFrame(s_targetFrame);
    s_lastSnapFrame = s_targetFrame;
  } else if (s_hasTime) {
    Playback::seekMs(s_targetMs);
    s_lastSnapMs = s_targetMs;
  }
}

static const char *opName(uint8_t op) {
  switch (op) {
  case kCueOpPlay:
    return "play";
  case kCueOpPause:
    return "pause";
  case kCueOpSeek:
    return "seek";
  case kCueOpTick:
    return "tick";
  default:
    return "op";
  }
}

static void becomeMaster() {
  s_master = true;
  s_waitMaster = false;
  if (s_follow) {
    s_follow = false;
    Playback::setLoop(true);
    s_loggedFollow = false;
  }
}

static void emitCue(uint8_t op, bool hasTime, bool hasFrame, uint32_t t_ms,
                    uint32_t frame) {
  if (!s_up) {
    return;
  }
  uint8_t pkt[kCuePktLenV2];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = static_cast<uint8_t>(kCueMagic0);
  pkt[1] = static_cast<uint8_t>(kCueMagic1);
  pkt[2] = static_cast<uint8_t>(kCueMagic2);
  pkt[3] = static_cast<uint8_t>(kCueMagic3);
  const bool v2 = s_group[0] != '\0';
  pkt[4] = v2 ? kCueVersion2 : kCueVersion;
  pkt[5] = op;
  uint8_t flags = 0;
  if (hasTime) {
    flags |= kCueFlagHasTime;
  }
  if (hasFrame) {
    flags |= kCueFlagHasFrame;
  }
  pkt[6] = flags;
  memcpy(pkt + 8, &t_ms, 4);
  memcpy(pkt + 12, &frame, 4);
  if (v2) {
    memcpy(pkt + 16, &s_groupHash, 4);
  }
  const uint8_t len = v2 ? kCuePktLenV2 : kCuePktLen;
  const IPAddress group = cueGroup();
  if (s_udp.beginPacket(group, kCuePort) != 1) {
    return;
  }
  s_udp.write(pkt, len);
  s_udp.endPacket();
  s_lastEmitMs = millis();
  s_lastEmitOp = op;
}

static void emitNow(uint8_t op) {
  uint32_t t_us = 0;
  uint32_t frame = 0;
  const bool has = Playback::peekFrame(t_us, frame);
  const uint32_t t_ms = has ? (t_us / 1000u) : 0;
  emitCue(op, has, has, t_ms, frame);
}

static void onCue(const CuePacket &p) {
  s_cueMs = millis();
  s_master = false;
  s_waitMaster = false;
  const bool first = !s_follow;
  enterFollow();

  const bool hasPos =
      (p.flags & (kCueFlagHasTime | kCueFlagHasFrame)) != 0;

  switch (p.opcode) {
  case kCueOpPlay: {
    const bool rising = first || !s_cuePlay;
    s_cuePlay = true;
    applyPosition(p.flags, p.t_ms, p.frame, rising);
    Playback::play();
    if (hasPos) {
      s_cuePulse = true;
    }
    if (rising) {
      LOG_V("sync", "cue play t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  }
  case kCueOpPause: {
    const bool falling = first || s_cuePlay;
    s_cuePlay = false;
    applyPosition(p.flags, p.t_ms, p.frame, hasPos);
    Playback::pause();
    if (falling) {
      LOG_V("sync", "cue pause t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  }
  case kCueOpSeek:
    applyPosition(p.flags, p.t_ms, p.frame, true);
    if (s_cuePlay && hasPos) {
      s_cuePulse = true;
    }
    if (p.t_ms != s_loggedSeekMs || p.frame != s_loggedSeekFrame) {
      s_loggedSeekMs = p.t_ms;
      s_loggedSeekFrame = p.frame;
      LOG_V("sync", "cue seek t_ms=%u frame=%u flags=%u",
            static_cast<unsigned>(p.t_ms), static_cast<unsigned>(p.frame),
            p.flags);
    }
    break;
  case kCueOpTick:
    applyPosition(p.flags, p.t_ms, p.frame, first);
    if (s_cuePlay && hasPos) {
      s_cuePulse = true;
    }
    break;
  default:
    LOG_V("sync", "cue %s ignored", opName(p.opcode));
    break;
  }
}

static void parseCue(int n, const IPAddress &from) {
  if (ownPacket(from)) {
    return;
  }
  if (n < static_cast<int>(kCuePktLen)) {
    return;
  }
  if (!magicOk(s_pkt)) {
    return;
  }
  const uint8_t ver = s_pkt[4];
  uint32_t group = 0;
  if (ver == kCueVersion2) {
    if (n < static_cast<int>(kCuePktLenV2)) {
      return;
    }
    memcpy(&group, s_pkt + 16, 4);
    if (s_group[0] && group != s_groupHash) {
      return;
    }
  } else if (ver == kCueVersion) {
    if (s_group[0]) {
      return;
    }
  } else {
    return;
  }
  CuePacket p;
  memcpy(&p, s_pkt, sizeof(p));
  onCue(p);
}

} // namespace

void Sync::begin() {
  if (s_begun) {
    return;
  }
  s_begun = true;
  if (!bindUnicast()) {
    return;
  }
  LOG_V("sync", "listen :%u (auto-play until cue)", kCuePort);
  joinMcast();
}

void Sync::service() {
  if (!s_begun) {
    return;
  }
  if (s_up && !s_mcast) {
    joinMcast();
  }
  if (!s_up && s_begun) {
    bindUnicast();
  }

  if (s_up) {
    for (;;) {
      const int n = s_udp.parsePacket();
      if (n <= 0) {
        break;
      }
      const IPAddress from = s_udp.remoteIP();
      const int got =
          s_udp.read(s_pkt, n > static_cast<int>(kCueMaxPkt) ? kCueMaxPkt : n);
      if (got > 0) {
        parseCue(got, from);
      }
    }
  }

  const uint32_t now = millis();
  if (s_liveActive &&
      (s_liveSyncMs == 0 || now - s_liveSyncMs >= kLiveSyncHoldMs)) {
    s_liveActive = false;
    s_liveFence = false;
    LOG_V("sync", "live free-run (no ArtSync/E1.31)");
  }
  if (s_waitMaster && (now - s_waitMs >= kCueHoldMs)) {
    LOG_V("sync", "cue wait timeout → master");
    becomeMaster();
    Playback::play();
    emitNow(kCueOpPlay);
  }
  if (s_follow && (s_cueMs == 0 || now - s_cueMs >= kCueHoldMs)) {
    leaveFollow();
  }
  if (s_master && !s_follow && Playback::playing()) {
    const uint32_t gap = LiveCfg::showIntervalMs();
    if (s_lastEmitMs == 0 || now - s_lastEmitMs >= gap) {
      emitNow(kCueOpTick);
    }
  }
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

bool Sync::cueFollow() { return s_follow; }

bool Sync::cuePlaying() { return s_cuePlay; }

bool Sync::cueSocketUp() { return s_up; }

bool Sync::hasCuePulse() { return s_follow && s_cuePlay && s_cuePulse; }

bool Sync::takeCuePulse() {
  if (!s_cuePulse) {
    return false;
  }
  s_cuePulse = false;
  return true;
}

bool Sync::cueHasTime() { return s_hasTime; }

bool Sync::cueHasFrame() { return s_hasFrame; }

uint32_t Sync::cueTargetMs() { return s_targetMs; }

uint32_t Sync::cueTargetFrame() { return s_targetFrame; }

void Sync::onPlayFile(const char *path) {
  const bool keepMaster = s_pendingLocal;
  clearGroup();
  if (!path || !path[0]) {
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  char side[64];
  if (!sidecarPath(path, side, sizeof(side))) {
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  if (!SD.exists(side)) {
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  File f = SD.open(side, FILE_READ);
  if (!f) {
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  char buf[768];
  const int n = f.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf) - 1);
  f.close();
  if (n <= 0) {
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  buf[n] = '\0';
  if (!extractJsonString(buf, "group", s_group, sizeof(s_group))) {
    s_group[0] = '\0';
  }
  parseMembers(buf);
  if (!s_group[0] || s_memberN < 2) {
    s_group[0] = '\0';
    s_groupHash = 0;
    s_memberN = 0;
    if (keepMaster) {
      s_master = true;
    }
    s_pendingLocal = false;
    return;
  }
  s_groupHash = hashGroup(s_group);
  if (keepMaster) {
    s_pendingLocal = false;
    s_master = true;
    s_waitMaster = false;
    LOG_V("sync", "group %s local master", s_group);
    return;
  }
  if (weAreElected()) {
    s_master = true;
    s_waitMaster = false;
    LOG_V("sync", "group %s master", s_group);
  } else {
    s_master = false;
    s_waitMaster = true;
    s_waitMs = millis();
    LOG_V("sync", "group %s wait", s_group);
  }
}

void Sync::noteLocalTrigger() {
  s_pendingLocal = true;
  becomeMaster();
  emitNow(kCueOpPlay);
}

void Sync::noteLocalPause() {
  s_pendingLocal = true;
  becomeMaster();
  emitNow(kCueOpPause);
}

void Sync::noteAutoStart() {
  if (!s_group[0]) {
    return;
  }
  if (s_waitMaster) {
    return;
  }
  if (s_master) {
    emitNow(kCueOpPlay);
  }
}

bool Sync::waitingForMaster() { return s_waitMaster && !s_follow; }

bool Sync::isMaster() { return s_master && !s_follow; }

bool Sync::hasGroup() { return s_group[0] != '\0'; }

const char *Sync::groupId() { return s_group; }

uint8_t Sync::memberCount() { return s_memberN; }

const char *Sync::memberNameAt(uint8_t i) {
  return i < s_memberN ? s_memberName[i] : "";
}
