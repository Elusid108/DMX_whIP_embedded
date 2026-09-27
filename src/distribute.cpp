#include "distribute.h"

#include "artnet_rx.h"
#include "dmxrec.h"
#include "log.h"
#include "node_id.h"
#include "playback.h"
#include "sd_info.h"
#include "sync_net.h"

#include <SD.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <cstdlib>
#include <cstring>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

static constexpr char kTmp[] = "/.whip-dist.tmp";
static constexpr uint32_t kFrameWindowMs = 4;
static constexpr uint8_t kMaxUnis = 16;
static constexpr uint8_t kMaxTargets = 12;
static constexpr size_t kStatusMax = 16384;
static constexpr uint32_t kHttpTimeoutMs = 8000;
static constexpr uint16_t kCopyBlock = 4096;
static constexpr uint16_t kReadRecords = 16;

enum class State : uint8_t { Idle, Running, Done, Error };

struct Target {
  IPAddress ip;
  char name[64];
  bool ok;
};

static volatile State s_state = State::Idle;
static char s_path[kSdPathLen];
static char s_msg[64];
static char s_peer[64];
static uint8_t s_index = 0;
static uint8_t s_count = 0;
static uint8_t s_ok = 0;
static uint8_t s_failed = 0;
static uint32_t s_sent = 0;
static uint32_t s_total = 0;
static Target s_targets[kMaxTargets];

// ---------------------------------------------------------------- tiny JSON

static const char *findKey(const char *from, const char *end, const char *key) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const size_t n = strlen(needle);
  for (const char *p = from; p && p + n <= end; ++p) {
    p = static_cast<const char *>(memchr(p, '"', static_cast<size_t>(end - p)));
    if (!p || p + n > end) {
      return nullptr;
    }
    if (memcmp(p, needle, n) == 0) {
      const char *c = p + n;
      while (c < end && (*c == ' ' || *c == ':')) {
        ++c;
      }
      return c;
    }
  }
  return nullptr;
}

static long jsonInt(const char *from, const char *end, const char *key,
                    long fallback) {
  const char *v = findKey(from, end, key);
  return v ? strtol(v, nullptr, 10) : fallback;
}

static bool jsonStr(const char *from, const char *end, const char *key,
                    char *out, size_t n) {
  const char *v = findKey(from, end, key);
  if (!v || *v != '"') {
    return false;
  }
  ++v;
  size_t i = 0;
  while (v < end && *v != '"' && i + 1 < n) {
    out[i++] = *v++;
  }
  out[i] = '\0';
  return true;
}

// ---------------------------------------------------------------- HTTP

static bool httpGet(const IPAddress &ip, const char *path, char *buf,
                    size_t cap, size_t &len) {
  WiFiClient c;
  c.setTimeout(kHttpTimeoutMs / 1000);
  if (!c.connect(ip, 80, kHttpTimeoutMs)) {
    return false;
  }
  c.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
           ip.toString().c_str());
  len = 0;
  const uint32_t t0 = millis();
  while ((c.connected() || c.available()) && millis() - t0 < kHttpTimeoutMs) {
    const int a = c.available();
    if (a <= 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    const size_t room = cap - 1 - len;
    if (room == 0) {
      break;
    }
    len += c.read(reinterpret_cast<uint8_t *>(buf + len),
                  static_cast<size_t>(a) < room ? a : room);
  }
  buf[len] = '\0';
  c.stop();
  return strncmp(buf, "HTTP/1.1 200", 12) == 0 || strncmp(buf, "HTTP/1.0 200", 12) == 0;
}

static int readStatusCode(WiFiClient &c) {
  const uint32_t t0 = millis();
  char line[40];
  size_t i = 0;
  while (millis() - t0 < kHttpTimeoutMs * 4) {
    if (!c.available()) {
      if (!c.connected()) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    const int ch = c.read();
    if (ch == '\n' || i + 1 >= sizeof(line)) {
      break;
    }
    line[i++] = static_cast<char>(ch);
  }
  line[i] = '\0';
  const char *sp = strchr(line, ' ');
  return sp ? atoi(sp + 1) : 0;
}

static bool postForm(const IPAddress &ip, const char *path, const String &body) {
  WiFiClient c;
  if (!c.connect(ip, 80, kHttpTimeoutMs)) {
    return false;
  }
  c.printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: "
           "application/x-www-form-urlencoded\r\nContent-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           path, ip.toString().c_str(), static_cast<unsigned>(body.length()));
  c.print(body);
  const int code = readStatusCode(c);
  c.stop();
  return code == 200;
}

static String urlEncode(const char *s) {
  String out;
  static const char hex[] = "0123456789ABCDEF";
  for (; *s; ++s) {
    const uint8_t ch = static_cast<uint8_t>(*s);
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += hex[ch >> 4];
      out += hex[ch & 15];
    }
  }
  return out;
}

// Stream kTmp to the peer's /upload as multipart path + file.
static bool upload(const IPAddress &ip, const char *destPath, uint32_t size) {
  WiFiClient c;
  if (!c.connect(ip, 80, kHttpTimeoutMs)) {
    return false;
  }
  const char *base = strrchr(destPath, '/');
  base = base ? base + 1 : destPath;
  const char *bnd = "----whipdist";
  String head = String("--") + bnd + "\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n" +
                destPath + "\r\n--" + bnd +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + base +
                "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  String tail = String("\r\n--") + bnd + "--\r\n";
  const uint32_t total = head.length() + size + tail.length();
  c.printf("POST /upload?path=%s HTTP/1.1\r\nHost: %s\r\nContent-Type: "
           "multipart/form-data; boundary=%s\r\nContent-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           urlEncode(destPath).c_str(), ip.toString().c_str(), bnd,
           static_cast<unsigned>(total));
  c.print(head);
  uint8_t *buf = static_cast<uint8_t *>(malloc(kCopyBlock));
  if (!buf) {
    c.stop();
    return false;
  }
  s_sent = 0;
  s_total = size;
  bool ok = true;
  uint32_t off = 0;
  File f;
  if (SdInfo::lock(1000)) {
    f = SD.open(kTmp, FILE_READ);
    SdInfo::unlock();
  }
  ok = static_cast<bool>(f);
  while (ok && off < size) {
    int got = 0;
    if (SdInfo::lock(1000)) {
      got = f.read(buf, kCopyBlock);
      SdInfo::unlock();
    }
    if (got <= 0) {
      ok = false;
      break;
    }
    size_t w = 0;
    const uint32_t t0 = millis();
    while (w < static_cast<size_t>(got) && millis() - t0 < kHttpTimeoutMs) {
      const size_t n = c.write(buf + w, static_cast<size_t>(got) - w);
      if (n == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (!c.connected()) {
          break;
        }
        continue;
      }
      w += n;
    }
    if (w != static_cast<size_t>(got)) {
      ok = false;
      break;
    }
    off += static_cast<uint32_t>(got);
    s_sent = off;
  }
  free(buf);
  if (f && SdInfo::lock(1000)) {
    f.close();
    SdInfo::unlock();
  }
  if (ok) {
    c.print(tail);
    ok = readStatusCode(c) == 200;
  }
  c.stop();
  return ok;
}

// ---------------------------------------------------------------- slicing

struct Window {
  bool ok;
  uint8_t srcProto;  // kDmxrecProto*
  uint8_t destProto;
  int32_t uniShift;
  uint32_t first;  // absolute address in source numbering
  uint32_t last;
};

// Output 0 of a peer /status: first segment's start, total capacity.
static bool peerWindow(const char *body, const char *end, uint8_t fileProto,
                       Window &w, char *name, size_t nameLen) {
  w = {};
  if (jsonInt(body, end, "api", 0) < kSyncApi) {
    return false;
  }
  const char *live = findKey(body, end, "live");
  if (live && strncmp(live, "true", 4) == 0) {
    return false;
  }
  jsonStr(body, end, "name", name, nameLen);
  const char *outs = findKey(body, end, "outputs");
  const char *segs = outs ? findKey(outs, end, "segs") : nullptr;
  if (!segs || *segs != '[') {
    return false;
  }
  const char *close = strchr(segs, ']');
  if (!close) {
    return false;
  }
  char proto[8] = "auto";
  long artnet = 0;
  long sacn = 1;
  long ch = 1;
  uint32_t capacity = 0;
  bool first = true;
  for (const char *o = strchr(segs, '{'); o && o < close; o = strchr(o + 1, '{')) {
    const char *oe = strchr(o, '}');
    if (!oe || oe > close) {
      break;
    }
    if (first) {
      jsonStr(o, oe, "proto", proto, sizeof(proto));
      artnet = jsonInt(o, oe, "artnet", 0);
      sacn = jsonInt(o, oe, "sacn", artnet + 1);
      ch = jsonInt(o, oe, "ch", 1);
      first = false;
    }
    capacity += static_cast<uint32_t>(jsonInt(o, oe, "count", 0)) *
                static_cast<uint32_t>(jsonInt(o, oe, "ch_px", 3));
    o = oe;
  }
  if (first || capacity == 0) {
    return false;
  }
  // Slice from the file's protocol; write in the peer's (renumbered).
  w.srcProto = fileProto;
  w.destProto = strcmp(proto, "sacn") == 0     ? kDmxrecProtoSacn
                : strcmp(proto, "artnet") == 0 ? kDmxrecProtoArtNet
                                               : fileProto;
  const long srcStart = w.srcProto == kDmxrecProtoSacn ? sacn : artnet;
  const long destStart = w.destProto == kDmxrecProtoSacn ? sacn : artnet;
  w.uniShift = static_cast<int32_t>(destStart - srcStart);
  w.first = static_cast<uint32_t>(srcStart) * kDmxrecDmxBytes +
            static_cast<uint32_t>(ch > 0 ? ch - 1 : 0);
  w.last = w.first + capacity - 1;
  w.ok = true;
  return true;
}

// Write kTmp: the window's universes, one record per source frame in which a
// contributing universe arrived, with every universe carried forward.
static bool sliceTo(const char *src, const Window &w, uint32_t &records) {
  records = 0;
  const uint32_t firstUni = w.first / kDmxrecDmxBytes;
  const uint32_t lastUni = w.last / kDmxrecDmxBytes;
  const uint32_t nUni = lastUni - firstUni + 1;
  if (nUni > kMaxUnis) {
    return false;
  }
  uint8_t *state = static_cast<uint8_t *>(calloc(nUni, kDmxrecDmxBytes));
  uint8_t *rec = static_cast<uint8_t *>(malloc(kDmxrecFrameBytes));
  if (!state || !rec) {
    free(state);
    free(rec);
    return false;
  }
  bool ok = true;
  uint32_t frames = 0;
  if (!SdInfo::lock(2000)) {
    free(state);
    free(rec);
    return false;
  }
  SD.remove(kTmp);
  File in = SD.open(src, FILE_READ);
  File out = SD.open(kTmp, FILE_WRITE);
  DmxrecHeader h = {};
  if (!in || !out ||
      in.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) != sizeof(h) ||
      !dmxrecMagicOk(h)) {
    ok = false;
  } else {
    DmxrecHeader oh = h;
    oh.frame_count = 0;
    out.write(reinterpret_cast<const uint8_t *>(&oh), sizeof(oh));
    frames = h.frame_count;
  }
  SdInfo::unlock();

  uint32_t dirty = 0;
  uint32_t seen = 0;
  bool haveGroup = false;
  uint32_t groupTs = 0;
  auto flush = [&]() {
    if (!haveGroup || !dirty) {
      dirty = 0;
      seen = 0;
      return;
    }
    for (uint32_t u = 0; u < nUni && ok; ++u) {
      if (!(dirty & (1u << u))) {
        continue;
      }
      DmxrecFramePrefix pre;
      pre.t_ms = groupTs;
      pre.universe = static_cast<uint32_t>(static_cast<int32_t>(firstUni + u) + w.uniShift);
      pre.protocol = w.destProto;
      memcpy(rec, &pre, sizeof(pre));
      memset(rec + sizeof(pre), 0, kDmxrecDmxBytes);
      const uint32_t base = (firstUni + u) * kDmxrecDmxBytes;
      for (uint32_t a = (base > w.first ? base : w.first);
           a <= w.last && a < base + kDmxrecDmxBytes; ++a) {
        rec[sizeof(pre) + (a - base)] = state[u * kDmxrecDmxBytes + (a - base)];
      }
      if (SdInfo::lock(1000)) {
        if (out.write(rec, kDmxrecFrameBytes) != kDmxrecFrameBytes) {
          ok = false;
        }
        SdInfo::unlock();
      } else {
        ok = false;
      }
      ++records;
    }
    dirty = 0;
    seen = 0;
  };

  uint8_t *block = static_cast<uint8_t *>(malloc(kDmxrecFrameBytes * kReadRecords));
  ok = ok && block != nullptr;
  uint32_t have = 0;
  uint32_t at = 0;
  for (uint32_t i = 0; ok && i < frames; ++i) {
    if (at >= have) {
      int got = 0;
      if (SdInfo::lock(1000)) {
        got = in.read(block, kDmxrecFrameBytes * kReadRecords);
        SdInfo::unlock();
      }
      have = got > 0 ? static_cast<uint32_t>(got) / kDmxrecFrameBytes : 0;
      at = 0;
      if (have == 0) {
        break;
      }
    }
    const uint8_t *buf = block + at * kDmxrecFrameBytes;
    ++at;
    DmxrecFramePrefix pre;
    memcpy(&pre, buf, sizeof(pre));
    if (pre.protocol != w.srcProto) {
      continue;
    }
    const bool inWin = pre.universe >= firstUni && pre.universe <= lastUni;
    const uint32_t bit = inWin ? (1u << (pre.universe - firstUni)) : 0;
    if (haveGroup && (pre.t_ms < groupTs || pre.t_ms - groupTs >= kFrameWindowMs ||
                      (seen & bit))) {
      flush();
      haveGroup = false;
    }
    if (!inWin) {
      continue;
    }
    if (!haveGroup) {
      haveGroup = true;
      groupTs = pre.t_ms;
    }
    seen |= bit;
    dirty |= bit;
    memcpy(state + (pre.universe - firstUni) * kDmxrecDmxBytes, buf + sizeof(pre),
           kDmxrecDmxBytes);
  }
  flush();
  free(block);

  if (SdInfo::lock(2000)) {
    if (ok && records > 0) {
      DmxrecHeader oh = h;
      oh.frame_count = records;
      out.seek(0);
      out.write(reinterpret_cast<const uint8_t *>(&oh), sizeof(oh));
    }
    if (in) {
      in.close();
    }
    if (out) {
      out.close();
    }
    SdInfo::unlock();
  }
  free(state);
  free(rec);
  return ok && records > 0;
}

static uint8_t fileProtocol(const char *path) {
  uint8_t proto = kDmxrecProtoArtNet;
  if (SdInfo::lock(1000)) {
    File f = SD.open(path, FILE_READ);
    DmxrecFramePrefix pre;
    if (f && f.seek(kDmxrecHeaderBytes) &&
        f.read(reinterpret_cast<uint8_t *>(&pre), sizeof(pre)) == sizeof(pre)) {
      proto = static_cast<uint8_t>(pre.protocol);
    }
    if (f) {
      f.close();
    }
    SdInfo::unlock();
  }
  return proto;
}

// Last record time plus one frame (25 ms), read from the file.
static uint32_t fileDurationMs(const char *path) {
  uint32_t dur = 0;
  if (SdInfo::lock(1000)) {
    File f = SD.open(path, FILE_READ);
    DmxrecHeader h;
    DmxrecFramePrefix pre;
    if (f && f.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h) &&
        h.frame_count > 0 && f.seek(dmxrecFrameOffset(h.frame_count - 1)) &&
        f.read(reinterpret_cast<uint8_t *>(&pre), sizeof(pre)) == sizeof(pre)) {
      dur = pre.t_ms + 25;
    }
    if (f) {
      f.close();
    }
    SdInfo::unlock();
  }
  return dur;
}

static void appendMember(String &out, const char *name) {
  out += "{\"n\":\"";
  for (const char *p = name; *p; ++p) {
    if (*p != '"' && *p != '\\' && static_cast<uint8_t>(*p) >= 0x20) {
      out += *p;
    }
  }
  out += "\",\"m\":\"\"}";
}

static String metaBody(const char *path, const char *title, const char *group,
                       const String &members, uint32_t dur) {
  String b = "path=" + urlEncode(path) + "&name=" + urlEncode(title) +
             "&sync_group=" + urlEncode(group) + "&sync_members=" +
             urlEncode(members.c_str()) + "&sync_kind=split&sync_dur=" + String(dur);
  return b;
}

static void finish(State st, const char *msg) {
  snprintf(s_msg, sizeof(s_msg), "%s", msg);
  s_peer[0] = '\0';
  s_state = st;
  LOG_V("dist", "%s", msg);
}

static void distTask(void *) {
  char *body = static_cast<char *>(psramFound() ? ps_malloc(kStatusMax) : malloc(kStatusMax));
  if (!body) {
    finish(State::Error, "out of memory");
    vTaskDelete(nullptr);
    return;
  }
  const uint8_t fileProto = fileProtocol(s_path);
  char title[kSdTitleLen] = {};
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (strcmp(SdInfo::fileAt(i), s_path) == 0) {
      snprintf(title, sizeof(title), "%s", SdInfo::titleAt(i));
    }
  }
  if (!title[0]) {
    const char *b = strrchr(s_path, '/');
    snprintf(title, sizeof(title), "%s", b ? b + 1 : s_path);
    char *dot = strrchr(title, '.');
    if (dot) {
      *dot = '\0';
    }
  }

  for (uint8_t i = 0; i < s_count; ++i) {
    Target &t = s_targets[i];
    s_index = i;
    snprintf(s_peer, sizeof(s_peer), "%s", t.name);
    size_t len = 0;
    Window w;
    char name[64] = {};
    if (!httpGet(t.ip, "/status", body, kStatusMax, len)) {
      ++s_failed;
      continue;
    }
    const char *json = strstr(body, "\r\n\r\n");
    json = json ? json + 4 : body;
    if (!peerWindow(json, body + len, fileProto, w, name, sizeof(name))) {
      ++s_failed;
      continue;
    }
    if (name[0]) {
      snprintf(t.name, sizeof(t.name), "%s", name);
      snprintf(s_peer, sizeof(s_peer), "%s", name);
    }
    uint32_t records = 0;
    if (!sliceTo(s_path, w, records)) {
      // Nothing of the show lands on this peer's patch.
      continue;
    }
    const uint32_t size = kDmxrecHeaderBytes + records * kDmxrecFrameBytes;
    if (upload(t.ip, s_path, size)) {
      t.ok = true;
      ++s_ok;
    } else {
      ++s_failed;
    }
  }
  if (SdInfo::lock(1000)) {
    SD.remove(kTmp);
    SdInfo::unlock();
  }

  if (s_ok == 0) {
    free(body);
    finish(State::Error, "no peer took a slice");
    vTaskDelete(nullptr);
    return;
  }
  char group[24];
  snprintf(group, sizeof(group), "d-%08x%08x", static_cast<unsigned>(esp_random()),
           static_cast<unsigned>(esp_random()));
  String members = "[";
  appendMember(members, NodeId::longName());
  for (uint8_t i = 0; i < s_count; ++i) {
    if (s_targets[i].ok) {
      members += ',';
      appendMember(members, s_targets[i].name);
    }
  }
  members += ']';
  const uint32_t dur = fileDurationMs(s_path);
  const String meta = metaBody(s_path, title, group, members, dur);
  for (uint8_t i = 0; i < s_count; ++i) {
    if (s_targets[i].ok && !postForm(s_targets[i].ip, "/meta", meta)) {
      ++s_failed;
    }
  }
  // Our own sidecar through this node's /meta (one writer for sidecars).
  if (!postForm(IPAddress(127, 0, 0, 1), "/meta", meta)) {
    const IPAddress self =
        WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
    postForm(self, "/meta", meta);
  }
  free(body);
  char msg[64];
  snprintf(msg, sizeof(msg), "sent to %u node%s%s", s_ok, s_ok == 1 ? "" : "s",
           s_failed ? " (some failed)" : "");
  finish(State::Done, msg);
  vTaskDelete(nullptr);
}

} // namespace

bool Distribute::running() { return s_state == State::Running; }

bool Distribute::start(const char *path, const char *&error) {
  error = nullptr;
  if (running()) {
    error = "busy";
    return false;
  }
  if (StreamTx::running()) {
    error = "streaming";
    return false;
  }
  if (!path || path[0] != '/' || !SdInfo::ok()) {
    error = "bad path";
    return false;
  }
  s_count = 0;
  for (uint8_t i = 0; i < SyncNet::peerCount() && s_count < kMaxTargets; ++i) {
    const SyncPeer *p = SyncNet::peerAt(i);
    if (p->role == static_cast<uint8_t>(SyncRole::Companion) || p->api < kSyncApi) {
      continue;
    }
    Target &t = s_targets[s_count++];
    t.ip = p->ip;
    snprintf(t.name, sizeof(t.name), "%s", p->name);
    t.ok = false;
  }
  if (s_count == 0) {
    error = "no peers";
    return false;
  }
  snprintf(s_path, sizeof(s_path), "%s", path);
  s_index = 0;
  s_ok = 0;
  s_failed = 0;
  s_sent = 0;
  s_total = 0;
  s_msg[0] = '\0';
  s_state = State::Running;
  if (xTaskCreate(distTask, "dist", 8192, nullptr, 1, nullptr) != pdPASS) {
    s_state = State::Error;
    error = "task";
    return false;
  }
  LOG_V("dist", "start %s to %u peers", path, s_count);
  return true;
}

void Distribute::appendStatus(String &out) {
  static const char *kNames[] = {"idle", "running", "done", "error"};
  out += "\"dist\":{\"state\":\"";
  out += kNames[static_cast<uint8_t>(s_state)];
  out += "\",\"path\":\"";
  out += s_path;
  out += "\",\"peer\":\"";
  out += s_peer;
  out += "\",\"i\":";
  out += static_cast<unsigned>(s_index);
  out += ",\"n\":";
  out += static_cast<unsigned>(s_count);
  out += ",\"ok\":";
  out += static_cast<unsigned>(s_ok);
  out += ",\"failed\":";
  out += static_cast<unsigned>(s_failed);
  out += ",\"sent\":";
  out += static_cast<unsigned>(s_sent);
  out += ",\"total\":";
  out += static_cast<unsigned>(s_total);
  out += ",\"msg\":\"";
  out += s_msg;
  out += "\"}";
}

// ================================================================== stream

namespace {

static constexpr uint8_t kStreamPeers = 12;
static constexpr uint16_t kSacnPort = 5568;
static constexpr uint32_t kStreamRewindSlackMs = 500;

struct StreamPeer {
  IPAddress ip;
  Window w;
  uint8_t seq[kMaxUnis];
};

static volatile bool s_streamOn = false;
static volatile bool s_streamStop = false;
static char s_streamPath[kSdPathLen];
static StreamPeer s_streamPeers[kStreamPeers];
static uint8_t s_streamN = 0;
static uint32_t s_streamPkts = 0;
static uint8_t s_cid[16];

static void artnetDmx(uint8_t *p, uint16_t universe, uint8_t seq,
                      const uint8_t *dmx) {
  memcpy(p, "Art-Net", 8);
  p[8] = 0x00;
  p[9] = 0x50;  // OpDmx 0x5000 LE
  p[10] = 0;
  p[11] = 14;
  p[12] = seq;
  p[13] = 0;
  p[14] = static_cast<uint8_t>(universe & 0xFF);
  p[15] = static_cast<uint8_t>((universe >> 8) & 0x7F);
  p[16] = 0x02;  // length 512, big-endian
  p[17] = 0x00;
  memcpy(p + 18, dmx, kDmxrecDmxBytes);
}

static void wr16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// E1.31 data packet (same bytes as the companion's encoder).
static void sacnDmx(uint8_t *p, uint16_t universe, uint8_t seq,
                    const uint8_t *dmx) {
  memset(p, 0, 126);
  wr16(p, 0x0010);
  memcpy(p + 4, "ASC-E1.17\0\0\0", 12);
  wr16(p + 16, 0x7000 | (638 - 16));
  wr32(p + 18, 4);
  memcpy(p + 22, s_cid, 16);
  wr16(p + 38, 0x7000 | (638 - 38));
  wr32(p + 40, 2);
  snprintf(reinterpret_cast<char *>(p + 44), 64, "%s", NodeId::longName());
  p[108] = 100;
  p[111] = seq;
  wr16(p + 113, universe);
  wr16(p + 115, 0x7000 | (638 - 115));
  p[117] = 0x02;
  p[118] = 0xa1;
  wr16(p + 121, 0x0001);
  wr16(p + 123, 0x0201);
  memcpy(p + 126, dmx, kDmxrecDmxBytes);
}

static void artSync(WiFiUDP &udp) {
  uint8_t p[14] = {'A', 'r', 't', '-', 'N', 'e', 't', 0, 0x00, 0x52, 0, 14, 0, 0};
  for (uint8_t i = 0; i < s_streamN; ++i) {
    if (s_streamPeers[i].w.destProto != kDmxrecProtoArtNet) {
      continue;
    }
    if (udp.beginPacket(s_streamPeers[i].ip, kArtNetPort) == 1) {
      udp.write(p, sizeof(p));
      udp.endPacket();
    }
  }
}

static void sendRecord(WiFiUDP &udp, const DmxrecFramePrefix &pre,
                       const uint8_t *dmx, uint8_t *pkt) {
  for (uint8_t i = 0; i < s_streamN; ++i) {
    StreamPeer &sp = s_streamPeers[i];
    const Window &w = sp.w;
    if (pre.protocol != w.srcProto) {
      continue;
    }
    const uint32_t firstUni = w.first / kDmxrecDmxBytes;
    const uint32_t lastUni = w.last / kDmxrecDmxBytes;
    if (pre.universe < firstUni || pre.universe > lastUni) {
      continue;
    }
    const uint32_t k = pre.universe - firstUni;
    const uint16_t uni =
        static_cast<uint16_t>(static_cast<int32_t>(pre.universe) + w.uniShift);
    sp.seq[k] = static_cast<uint8_t>(sp.seq[k] + 1);
    if (w.destProto == kDmxrecProtoSacn) {
      sacnDmx(pkt, uni, sp.seq[k], dmx);
      if (udp.beginPacket(sp.ip, kSacnPort) == 1) {
        udp.write(pkt, 638);
        udp.endPacket();
      }
    } else {
      artnetDmx(pkt, uni, sp.seq[k] ? sp.seq[k] : 1, dmx);
      if (udp.beginPacket(sp.ip, kArtNetPort) == 1) {
        udp.write(pkt, 18 + kDmxrecDmxBytes);
        udp.endPacket();
      }
    }
    ++s_streamPkts;
  }
}

// The show position of this node's own playback of the stream file.
static bool holderPos(uint32_t &pos) {
  if (!Playback::hasFile() || Playback::parked() ||
      strcmp(Playback::path(), s_streamPath) != 0) {
    return false;
  }
  pos = Playback::showPosMs(millis());
  return true;
}

static void streamTask(void *) {
  WiFiUDP udp;
  udp.begin(0);
  uint8_t *block = static_cast<uint8_t *>(malloc(kDmxrecFrameBytes * kReadRecords));
  uint8_t *pkt = static_cast<uint8_t *>(malloc(638));
  File f;
  uint32_t frames = 0;
  if (SdInfo::lock(1000)) {
    f = SD.open(s_streamPath, FILE_READ);
    DmxrecHeader h;
    if (f && f.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h) &&
        dmxrecMagicOk(h)) {
      frames = h.frame_count;
    }
    SdInfo::unlock();
  }
  uint32_t idx = 0;
  uint32_t have = 0;
  uint32_t at = 0;
  bool haveGroup = false;
  uint32_t groupTs = 0;
  uint32_t lastPos = 0;
  uint32_t idleMs = millis();
  // Records before the holder's position when (re)starting are skipped.
  bool catchUp = true;
  while (block && pkt && f && frames && !s_streamStop) {
    uint32_t pos = 0;
    if (!holderPos(pos)) {
      if (millis() - idleMs > 5000) {
        break;  // the holder stopped playing this show
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    idleMs = millis();
    // The holder looped or was seeked back: read from the top again.
    if (pos + kStreamRewindSlackMs < lastPos) {
      if (SdInfo::lock(1000)) {
        f.seek(kDmxrecHeaderBytes);
        SdInfo::unlock();
      }
      idx = 0;
      have = 0;
      at = 0;
      catchUp = true;
      haveGroup = false;
      groupTs = 0;
    }
    lastPos = pos;
    if (at >= have) {
      if (idx >= frames) {
        // End of file: hold until the holder loops.
        if (haveGroup) {
          artSync(udp);
          haveGroup = false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      int got = 0;
      if (SdInfo::lock(1000)) {
        got = f.read(block, kDmxrecFrameBytes * kReadRecords);
        SdInfo::unlock();
      }
      have = got > 0 ? static_cast<uint32_t>(got) / kDmxrecFrameBytes : 0;
      at = 0;
      if (have == 0) {
        idx = frames;
        continue;
      }
    }
    const uint8_t *rec = block + at * kDmxrecFrameBytes;
    DmxrecFramePrefix pre;
    memcpy(&pre, rec, sizeof(pre));
    if (pre.t_ms > pos) {
      if (catchUp) {
        catchUp = false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    ++at;
    ++idx;
    if (haveGroup && pre.t_ms - groupTs >= kFrameWindowMs) {
      artSync(udp);
    }
    haveGroup = true;
    groupTs = pre.t_ms;
    if (!catchUp || pos - pre.t_ms < 100) {
      sendRecord(udp, pre, rec + sizeof(pre), pkt);
    }
  }
  if (f && SdInfo::lock(1000)) {
    f.close();
    SdInfo::unlock();
  }
  free(block);
  free(pkt);
  udp.stop();
  s_streamOn = false;
  LOG_V("stream", "stopped");
  vTaskDelete(nullptr);
}

static void streamSetupTask(void *) {
  char *body = static_cast<char *>(psramFound() ? ps_malloc(kStatusMax) : malloc(kStatusMax));
  const uint8_t fileProto = fileProtocol(s_streamPath);
  s_streamN = 0;
  for (uint8_t i = 0; body && i < s_count && s_streamN < kStreamPeers; ++i) {
    size_t len = 0;
    Window w;
    char name[64] = {};
    if (!httpGet(s_targets[i].ip, "/status", body, kStatusMax, len)) {
      continue;
    }
    const char *json = strstr(body, "\r\n\r\n");
    json = json ? json + 4 : body;
    if (!peerWindow(json, body + len, fileProto, w, name, sizeof(name))) {
      continue;
    }
    StreamPeer &sp = s_streamPeers[s_streamN++];
    sp.ip = s_targets[i].ip;
    sp.w = w;
    memset(sp.seq, 0, sizeof(sp.seq));
  }
  free(body);
  LOG_V("stream", "%s to %u peers", s_streamPath, s_streamN);
  if (s_streamN == 0 ||
      xTaskCreate(streamTask, "stream", 6144, nullptr, 2, nullptr) != pdPASS) {
    s_streamOn = false;
  }
  vTaskDelete(nullptr);
}

} // namespace

bool StreamTx::start(const char *path, const char *&error) {
  error = nullptr;
  if (s_streamOn) {
    error = "busy";
    return false;
  }
  if (Distribute::running()) {
    error = "distributing";
    return false;
  }
  s_count = 0;
  for (uint8_t i = 0; i < SyncNet::peerCount() && s_count < kMaxTargets; ++i) {
    const SyncPeer *p = SyncNet::peerAt(i);
    if (p->role == static_cast<uint8_t>(SyncRole::Companion) || p->api < kSyncApi) {
      continue;
    }
    Target &t = s_targets[s_count++];
    t.ip = p->ip;
    snprintf(t.name, sizeof(t.name), "%s", p->name);
    t.ok = false;
  }
  if (s_count == 0) {
    error = "no peers";
    return false;
  }
  for (uint8_t i = 0; i < sizeof(s_cid); ++i) {
    s_cid[i] = static_cast<uint8_t>(esp_random());
  }
  snprintf(s_streamPath, sizeof(s_streamPath), "%s", path);
  s_streamStop = false;
  s_streamPkts = 0;
  s_streamOn = true;
  if (xTaskCreate(streamSetupTask, "streamset", 8192, nullptr, 1, nullptr) != pdPASS) {
    s_streamOn = false;
    error = "task";
    return false;
  }
  return true;
}

void StreamTx::stop() { s_streamStop = true; }

bool StreamTx::running() { return s_streamOn; }

void StreamTx::appendStatus(String &out) {
  out += "\"stream\":{\"on\":";
  out += s_streamOn ? "true" : "false";
  out += ",\"path\":\"";
  out += s_streamOn ? s_streamPath : "";
  out += "\",\"peers\":";
  out += static_cast<unsigned>(s_streamOn ? s_streamN : 0);
  out += ",\"pkts\":";
  out += static_cast<unsigned>(s_streamPkts);
  out += '}';
}
