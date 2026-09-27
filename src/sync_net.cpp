#include "sync_net.h"

#include "log.h"
#include "node_id.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

#include "esp_timer.h"

namespace {

static constexpr uint8_t kMaxPeers = 16;
static constexpr uint32_t kPeerTimeoutMs = 7000;
static constexpr uint32_t kHelloIdleMs = 2000;
static constexpr uint32_t kHelloCueMs = 1000;
static constexpr uint32_t kIncumbentMs = 5000;
static constexpr uint8_t kSamples = 8;
static constexpr uint32_t kPingFastMs = 1000;
static constexpr uint32_t kPingSlowMs = 4000;
static constexpr int64_t kStepUs = 30000;
static constexpr uint8_t kPendingPings = 4;
static constexpr uint8_t kQueueLen = 12;
static constexpr uint8_t kSeenLen = 48;
static constexpr uint32_t kSeenMs = 5000;
static constexpr uint8_t kRepeatSlots = 6;
static constexpr size_t kHdr = 20;
static constexpr size_t kMaxPkt = 128;

static WiFiUDP s_udp;
static bool s_up = false;
static uint32_t s_bindMs = 0;
static uint32_t s_self = 0;
static uint16_t s_seq = 0;
static SyncRole s_role = SyncRole::Node;

// Clock: masterUs = localUs + s_off. s_target is the best measured offset.
static int64_t s_off = 0;
static int64_t s_target = 0;
static int64_t s_stepPending = 0;
static int64_t s_lastSlewUs = 0;
static bool s_isMaster = true;
static uint32_t s_masterId = 0;
static uint32_t s_masterSinceMs = 0;
static bool s_everSynced = false;

struct Sample {
  int64_t off;
  uint32_t rtt;
};
static Sample s_samples[kSamples];
static uint8_t s_sampleN = 0;
static uint8_t s_sampleI = 0;
static uint32_t s_bestRtt = 0;
static int64_t s_pendingT1[kPendingPings];
static uint8_t s_pendingI = 0;
static uint32_t s_lastPingMs = 0;

static SyncPeer s_peers[kMaxPeers];
static uint8_t s_peerN = 0;

static CueSummary s_helloCue = {};
static bool s_helloHasCue = false;
static uint32_t s_helloWait = 0;
static uint32_t s_lastHelloMs = 0;
static bool s_helloSoon = true;

static CueMsg s_queue[kQueueLen];
static uint8_t s_qHead = 0;
static uint8_t s_qCount = 0;

struct Seen {
  uint32_t sender;
  uint16_t seq;
  uint32_t ms;
};
static Seen s_seen[kSeenLen];
static uint8_t s_seenI = 0;

struct Repeat {
  uint8_t buf[kMaxPkt];
  uint8_t len;
  uint8_t left;
  uint32_t nextMs;
};
static Repeat s_repeat[kRepeatSlots];

static uint8_t s_pkt[kMaxPkt];

// ------------------------------------------------------------------ bytes

static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, int64_t v) { memcpy(p, &v, 8); }
static uint16_t get16(const uint8_t *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}
static uint32_t get32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
static int64_t get64(const uint8_t *p) {
  int64_t v;
  memcpy(&v, p, 8);
  return v;
}

static size_t header(uint8_t *p, uint8_t op, uint8_t flags, uint16_t seq,
                     uint32_t group, uint32_t cue) {
  p[0] = 'W';
  p[1] = 'H';
  p[2] = 'P';
  p[3] = '3';
  p[4] = op;
  p[5] = flags;
  put16(p + 6, seq);
  put32(p + 8, s_self);
  put32(p + 12, group);
  put32(p + 16, cue);
  return kHdr;
}

// ------------------------------------------------------------------ net

static bool bindSocket() {
  if (s_up) {
    return true;
  }
  const uint32_t now = millis();
  if (s_bindMs != 0 && now - s_bindMs < 1000) {
    return false;
  }
  s_bindMs = now;
  if (!s_udp.begin(kSyncPort)) {
    LOG_C("sync", "udp bind :%u failed", kSyncPort);
    return false;
  }
  s_up = true;
  LOG_V("sync", "cue bus v3 :%u id=%08x", kSyncPort,
        static_cast<unsigned>(s_self));
  return true;
}

static void sendTo(const IPAddress &ip, const uint8_t *p, size_t n) {
  if (!s_up || !ip) {
    return;
  }
  if (s_udp.beginPacket(ip, kSyncPort) != 1) {
    return;
  }
  s_udp.write(p, n);
  s_udp.endPacket();
}

static IPAddress directed(const IPAddress &ip, const IPAddress &mask) {
  return IPAddress(ip[0] | static_cast<uint8_t>(~mask[0]),
                   ip[1] | static_cast<uint8_t>(~mask[1]),
                   ip[2] | static_cast<uint8_t>(~mask[2]),
                   ip[3] | static_cast<uint8_t>(~mask[3]));
}

// Directed broadcast on every interface that has an address.
static void broadcast(const uint8_t *p, size_t n) {
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress mask = WiFi.subnetMask();
    sendTo(mask ? directed(WiFi.localIP(), mask) : IPAddress(255, 255, 255, 255),
           p, n);
  }
  if (WiFi.getMode() & WIFI_AP) {
    const IPAddress ap = WiFi.softAPIP();
    if (ap) {
      sendTo(directed(ap, IPAddress(255, 255, 255, 0)), p, n);
    }
  }
}

// ------------------------------------------------------------------ peers

static SyncPeer *findPeer(uint32_t id) {
  for (uint8_t i = 0; i < s_peerN; ++i) {
    if (s_peers[i].id == id) {
      return &s_peers[i];
    }
  }
  return nullptr;
}

static void expirePeers(uint32_t now) {
  uint8_t w = 0;
  for (uint8_t i = 0; i < s_peerN; ++i) {
    if (now - s_peers[i].lastMs < kPeerTimeoutMs) {
      if (w != i) {
        s_peers[w] = s_peers[i];
      }
      ++w;
    }
  }
  s_peerN = w;
}

// ------------------------------------------------------------------ clock

static void resetSamples() {
  s_sampleN = 0;
  s_sampleI = 0;
  s_bestRtt = 0;
  for (uint8_t i = 0; i < kPendingPings; ++i) {
    s_pendingT1[i] = 0;
  }
}

// Rank: role, then the incumbent master, then the lowest id. A node that
// never synced (fresh boot, own timeline) only wins when no one is synced.
struct Cand {
  uint32_t id;
  uint8_t role;
  bool incumbent;
  bool synced;
};

static bool better(const Cand &a, const Cand &b) {
  if (a.role != b.role) {
    return a.role > b.role;
  }
  if (a.incumbent != b.incumbent) {
    return a.incumbent;
  }
  return a.id < b.id;
}

static void elect(uint32_t now) {
  Cand self = {s_self, static_cast<uint8_t>(s_role),
               s_isMaster && now - s_masterSinceMs >= kIncumbentMs,
               s_isMaster || s_everSynced};
  bool anySynced = self.synced;
  for (uint8_t i = 0; i < s_peerN; ++i) {
    anySynced = anySynced || s_peers[i].synced || s_peers[i].isMaster;
  }
  Cand best = self;
  bool haveBest = !anySynced || self.synced || self.role == 3;
  for (uint8_t i = 0; i < s_peerN; ++i) {
    const SyncPeer &p = s_peers[i];
    if (p.api < kSyncApi) {
      continue;
    }
    Cand c = {p.id, p.role, p.isMaster && p.masterForMs >= kIncumbentMs,
              p.synced || p.isMaster};
    if (anySynced && !c.synced && c.role != 3) {
      continue;
    }
    if (!haveBest || better(c, best)) {
      best = c;
      haveBest = true;
    }
  }
  const uint32_t want = best.id;
  if (want == s_masterId && (s_masterId != 0)) {
    return;
  }
  const bool nowMaster = want == s_self;
  if (nowMaster != s_isMaster || want != s_masterId) {
    LOG_V("sync", "clock master %08x%s", static_cast<unsigned>(want),
          nowMaster ? " (self)" : "");
  }
  if (nowMaster && !s_isMaster) {
    // Keep the timeline we were following.
    s_target = s_off;
    s_masterSinceMs = now;
  } else if (nowMaster && s_masterId == 0) {
    s_masterSinceMs = now;
  }
  s_isMaster = nowMaster;
  s_masterId = want;
  resetSamples();
  s_lastPingMs = 0;
  s_helloSoon = true;
}

static void addSample(int64_t off, uint32_t rtt) {
  s_samples[s_sampleI] = {off, rtt};
  s_sampleI = static_cast<uint8_t>((s_sampleI + 1) % kSamples);
  if (s_sampleN < kSamples) {
    ++s_sampleN;
  }
  uint8_t best = 0;
  for (uint8_t i = 1; i < s_sampleN; ++i) {
    if (s_samples[i].rtt < s_samples[best].rtt) {
      best = i;
    }
  }
  s_target = s_samples[best].off;
  s_bestRtt = s_samples[best].rtt;
  if (!s_everSynced || s_target - s_off > kStepUs || s_off - s_target > kStepUs) {
    s_stepPending += s_target - s_off;
    s_off = s_target;
    if (s_everSynced) {
      LOG_V("sync", "clock step %lld us", static_cast<long long>(s_stepPending));
    }
  }
  s_everSynced = true;
}

static void slew() {
  const int64_t now = esp_timer_get_time();
  const int64_t dt = s_lastSlewUs ? now - s_lastSlewUs : 0;
  s_lastSlewUs = now;
  if (s_isMaster || dt <= 0) {
    return;
  }
  const int64_t maxAdj = dt / 200;  // 0.5 %
  const int64_t d = s_target - s_off;
  if (d > maxAdj) {
    s_off += maxAdj;
  } else if (d < -maxAdj) {
    s_off -= maxAdj;
  } else {
    s_off = s_target;
  }
}

static void sendPing(uint32_t now) {
  const SyncPeer *m = findPeer(s_masterId);
  if (!m) {
    return;
  }
  const uint32_t gap = s_sampleN < kSamples ? kPingFastMs : kPingSlowMs;
  if (s_lastPingMs != 0 && now - s_lastPingMs < gap) {
    return;
  }
  s_lastPingMs = now;
  uint8_t p[kHdr + 8];
  header(p, kSyncOpPing, 0, ++s_seq, 0, 0);
  const int64_t t1 = esp_timer_get_time();
  put64(p + kHdr, t1);
  s_pendingT1[s_pendingI] = t1;
  s_pendingI = static_cast<uint8_t>((s_pendingI + 1) % kPendingPings);
  sendTo(m->ip, p, sizeof(p));
}

// ------------------------------------------------------------------ hello

static void sendHello() {
  uint8_t p[kMaxPkt];
  memset(p, 0, sizeof(p));
  const CueSummary &c = s_helloCue;
  uint8_t flags = 0;
  if (c.loop) {
    flags |= 1;
  }
  header(p, kSyncOpHello, flags, ++s_seq, s_helloHasCue ? c.group : 0,
         s_helloHasCue ? c.cue : 0);
  uint8_t *b = p + kHdr;
  b[0] = static_cast<uint8_t>(s_role);
  b[1] = kSyncApi;
  uint8_t hf = 0;
  if (s_isMaster || s_everSynced) {
    hf |= 1;
  }
  if (s_isMaster) {
    hf |= 2;
  }
  if (s_helloHasCue) {
    hf |= 4;
    if (c.paused) {
      hf |= 8;
    }
    if (c.loop) {
      hf |= 16;
    }
  }
  b[2] = hf;
  put32(b + 4, s_masterId);
  put32(b + 8, s_isMaster ? 0 : s_bestRtt);
  put32(b + 12, s_helloWait);
  snprintf(reinterpret_cast<char *>(b + 16), 24, "%s", NodeId::longName());
  size_t n = kHdr + 40;
  if (s_helloHasCue) {
    put64(p + n, c.createdAt);
    put64(p + n + 8, c.startAt);
    put32(p + n + 16, c.startPos);
    put32(p + n + 20, c.dur);
    put32(p + n + 24, c.pausePos);
    n += 28;
  }
  broadcast(p, n);
}

// ------------------------------------------------------------------ rx

static bool seenBefore(uint32_t sender, uint16_t seq, uint32_t now) {
  for (uint8_t i = 0; i < kSeenLen; ++i) {
    if (s_seen[i].sender == sender && s_seen[i].seq == seq &&
        now - s_seen[i].ms < kSeenMs) {
      return true;
    }
  }
  s_seen[s_seenI] = {sender, seq, now};
  s_seenI = static_cast<uint8_t>((s_seenI + 1) % kSeenLen);
  return false;
}

static void enqueue(const CueMsg &m) {
  if (s_qCount == kQueueLen) {
    s_qHead = static_cast<uint8_t>((s_qHead + 1) % kQueueLen);
    --s_qCount;
  }
  s_queue[(s_qHead + s_qCount) % kQueueLen] = m;
  ++s_qCount;
}

static void onHello(const uint8_t *p, int n, const IPAddress &from,
                    uint32_t now) {
  if (n < static_cast<int>(kHdr + 40)) {
    return;
  }
  const uint32_t id = get32(p + 8);
  SyncPeer *peer = findPeer(id);
  if (!peer) {
    if (s_peerN >= kMaxPeers) {
      return;
    }
    peer = &s_peers[s_peerN++];
    memset(peer, 0, sizeof(*peer));
    peer->id = id;
    s_helloSoon = true;
  }
  const uint8_t *b = p + kHdr;
  const uint8_t hf = b[2];
  const bool wasMaster = peer->isMaster;
  peer->ip = from;
  peer->role = b[0];
  peer->api = b[1];
  peer->synced = (hf & 1) != 0;
  peer->isMaster = (hf & 2) != 0;
  if (peer->isMaster && !wasMaster) {
    peer->masterForMs = 0;
  }
  if (peer->isMaster && peer->lastMs) {
    peer->masterForMs += now - peer->lastMs;
  }
  peer->master = get32(b + 4);
  peer->rttUs = get32(b + 8);
  peer->waitGroup = get32(b + 12);
  memcpy(peer->name, b + 16, 23);
  peer->name[23] = '\0';
  peer->lastMs = now;
  peer->hasCue = (hf & 4) != 0 && n >= static_cast<int>(kHdr + 68);
  if (peer->hasCue) {
    const uint8_t *c = p + kHdr + 40;
    peer->cue.group = get32(p + 12);
    peer->cue.cue = get32(p + 16);
    peer->cue.createdAt = get64(c);
    peer->cue.startAt = get64(c + 8);
    peer->cue.startPos = get32(c + 16);
    peer->cue.dur = get32(c + 20);
    peer->cue.pausePos = get32(c + 24);
    peer->cue.paused = (hf & 8) != 0;
    peer->cue.loop = (hf & 16) != 0;
    CueMsg m = {};
    m.op = kSyncOpState;
    m.sender = id;
    m.group = peer->cue.group;
    m.cue = peer->cue.cue;
    m.createdAt = peer->cue.createdAt;
    m.startAt = peer->cue.startAt;
    m.startPos = peer->cue.startPos;
    m.dur = peer->cue.dur;
    m.pos = peer->cue.pausePos;
    m.loop = peer->cue.loop;
    m.paused = peer->cue.paused;
    enqueue(m);
  }
}

static void onPacket(const uint8_t *p, int n, const IPAddress &from) {
  if (n < static_cast<int>(kHdr) || p[0] != 'W' || p[1] != 'H' || p[2] != 'P' ||
      p[3] != '3') {
    return;
  }
  const uint32_t sender = get32(p + 8);
  if (sender == s_self) {
    return;
  }
  const uint8_t op = p[4];
  const uint8_t flags = p[5];
  const uint16_t seq = get16(p + 6);
  const uint32_t now = millis();

  if (op == kSyncOpHello) {
    onHello(p, n, from, now);
    return;
  }
  if (op == kSyncOpPing) {
    if (!s_isMaster || n < static_cast<int>(kHdr + 8)) {
      return;
    }
    const int64_t t2 = esp_timer_get_time() + s_off;
    uint8_t r[kHdr + 24];
    header(r, kSyncOpPong, 0, ++s_seq, 0, 0);
    memcpy(r + kHdr, p + kHdr, 8);
    put64(r + kHdr + 8, t2);
    put64(r + kHdr + 16, esp_timer_get_time() + s_off);
    sendTo(from, r, sizeof(r));
    return;
  }
  if (op == kSyncOpPong) {
    if (s_isMaster || sender != s_masterId || n < static_cast<int>(kHdr + 24)) {
      return;
    }
    const int64_t t4 = esp_timer_get_time();
    const int64_t t1 = get64(p + kHdr);
    bool mine = false;
    for (uint8_t i = 0; i < kPendingPings; ++i) {
      if (s_pendingT1[i] == t1 && t1 != 0) {
        s_pendingT1[i] = 0;
        mine = true;
      }
    }
    if (!mine) {
      return;
    }
    const int64_t t2 = get64(p + kHdr + 8);
    const int64_t t3 = get64(p + kHdr + 16);
    const int64_t rtt = (t4 - t1) - (t3 - t2);
    if (rtt < 0 || rtt > 500000) {
      return;
    }
    addSample(((t2 - t1) + (t3 - t4)) / 2, static_cast<uint32_t>(rtt));
    return;
  }
  if (op < kSyncOpLaunch || op > kSyncOpStop) {
    return;
  }
  if (seenBefore(sender, seq, now)) {
    return;
  }
  CueMsg m = {};
  m.op = op;
  m.loop = (flags & 1) != 0;
  m.paused = (flags & 2) != 0;
  m.sender = sender;
  m.group = get32(p + 12);
  m.cue = get32(p + 16);
  const uint8_t *b = p + kHdr;
  switch (op) {
  case kSyncOpLaunch:
    if (n < static_cast<int>(kHdr + 24)) {
      return;
    }
    m.createdAt = get64(b);
    m.startAt = get64(b + 8);
    m.startPos = get32(b + 16);
    m.dur = get32(b + 20);
    break;
  case kSyncOpPause:
    if (n < static_cast<int>(kHdr + 12)) {
      return;
    }
    m.at = get64(b);
    m.pos = get32(b + 8);
    break;
  case kSyncOpResume:
  case kSyncOpSeek:
    if (n < static_cast<int>(kHdr + 12)) {
      return;
    }
    m.startAt = get64(b);
    m.startPos = get32(b + 8);
    break;
  case kSyncOpStop:
    if (n < static_cast<int>(kHdr + 8)) {
      return;
    }
    m.at = get64(b);
    break;
  default:
    return;
  }
  enqueue(m);
}

static void serviceRepeats(uint32_t now) {
  for (uint8_t i = 0; i < kRepeatSlots; ++i) {
    Repeat &r = s_repeat[i];
    if (!r.left || static_cast<int32_t>(now - r.nextMs) < 0) {
      continue;
    }
    for (uint8_t k = 0; k < s_peerN; ++k) {
      sendTo(s_peers[k].ip, r.buf, r.len);
    }
    broadcast(r.buf, r.len);
    --r.left;
    r.nextMs = now + 40;
  }
}

} // namespace

void SyncNet::begin() {
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  uint32_t h = 2166136261u;
  for (uint8_t b : mac) {
    h ^= b;
    h *= 16777619u;
  }
  s_self = h ? h : 1;
  s_masterId = s_self;
  s_isMaster = true;
  s_masterSinceMs = millis();
  bindSocket();
}

void SyncNet::service() {
  if (!bindSocket()) {
    return;
  }
  for (;;) {
    const int n = s_udp.parsePacket();
    if (n <= 0) {
      break;
    }
    const IPAddress from = s_udp.remoteIP();
    const int got = s_udp.read(s_pkt, n > static_cast<int>(kMaxPkt) ? kMaxPkt : n);
    if (got > 0) {
      onPacket(s_pkt, got, from);
    }
  }
  const uint32_t now = millis();
  expirePeers(now);
  elect(now);
  slew();
  if (!s_isMaster) {
    sendPing(now);
  }
  serviceRepeats(now);
  const uint32_t gap = s_helloHasCue ? kHelloCueMs : kHelloIdleMs;
  if (s_helloSoon || now - s_lastHelloMs >= gap) {
    s_helloSoon = false;
    s_lastHelloMs = now;
    sendHello();
  }
}

uint32_t SyncNet::selfId() { return s_self; }

int64_t SyncNet::localUs() { return esp_timer_get_time(); }

int64_t SyncNet::masterUs() { return esp_timer_get_time() + s_off; }

bool SyncNet::isClockMaster() { return s_isMaster; }

uint32_t SyncNet::clockMasterId() { return s_masterId; }

bool SyncNet::synced() { return s_isMaster || s_everSynced; }

uint32_t SyncNet::rttUs() { return s_isMaster ? 0 : s_bestRtt; }

int64_t SyncNet::takeClockStep() {
  const int64_t d = s_stepPending;
  s_stepPending = 0;
  return d;
}

void SyncNet::setRole(SyncRole role) {
  s_role = role;
  s_helloSoon = true;
}

SyncRole SyncNet::role() { return s_role; }

void SyncNet::setHelloState(const CueSummary *cue, uint32_t waitGroup) {
  const bool had = s_helloHasCue;
  const uint32_t oldCue = s_helloCue.cue;
  const bool oldPaused = s_helloCue.paused;
  s_helloHasCue = cue != nullptr;
  if (cue) {
    s_helloCue = *cue;
  }
  if (had != s_helloHasCue || (cue && (cue->cue != oldCue || cue->paused != oldPaused)) ||
      waitGroup != s_helloWait) {
    s_helloSoon = true;
  }
  s_helloWait = waitGroup;
}

void SyncNet::helloSoon() { s_helloSoon = true; }

bool SyncNet::pollCue(CueMsg &out) {
  if (!s_qCount) {
    return false;
  }
  out = s_queue[s_qHead];
  s_qHead = static_cast<uint8_t>((s_qHead + 1) % kQueueLen);
  --s_qCount;
  return true;
}

void SyncNet::sendCue(const CueMsg &m) {
  uint8_t flags = 0;
  if (m.loop) {
    flags |= 1;
  }
  if (m.paused) {
    flags |= 2;
  }
  Repeat *slot = &s_repeat[0];
  for (uint8_t i = 0; i < kRepeatSlots; ++i) {
    if (!s_repeat[i].left) {
      slot = &s_repeat[i];
      break;
    }
  }
  uint8_t *p = slot->buf;
  memset(p, 0, kMaxPkt);
  header(p, m.op, flags, ++s_seq, m.group, m.cue);
  uint8_t *b = p + kHdr;
  size_t n = kHdr;
  switch (m.op) {
  case kSyncOpLaunch:
    put64(b, m.createdAt);
    put64(b + 8, m.startAt);
    put32(b + 16, m.startPos);
    put32(b + 20, m.dur);
    n += 24;
    break;
  case kSyncOpPause:
    put64(b, m.at);
    put32(b + 8, m.pos);
    n += 12;
    break;
  case kSyncOpResume:
  case kSyncOpSeek:
    put64(b, m.startAt);
    put32(b + 8, m.startPos);
    n += 12;
    break;
  case kSyncOpStop:
    put64(b, m.at);
    n += 8;
    break;
  default:
    return;
  }
  slot->len = static_cast<uint8_t>(n);
  for (uint8_t k = 0; k < s_peerN; ++k) {
    sendTo(s_peers[k].ip, p, n);
  }
  broadcast(p, n);
  slot->left = 2;
  slot->nextMs = millis() + 20;
}

uint8_t SyncNet::peerCount() { return s_peerN; }

const SyncPeer *SyncNet::peerAt(uint8_t i) {
  return i < s_peerN ? &s_peers[i] : nullptr;
}

bool SyncNet::lowestWaiting(uint32_t group) {
  for (uint8_t i = 0; i < s_peerN; ++i) {
    if (s_peers[i].waitGroup == group && s_peers[i].id < s_self) {
      return false;
    }
  }
  return true;
}
