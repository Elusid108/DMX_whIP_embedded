#include "ota.h"

#include "board_profile.h"
#include "distribute.h"
#include "live_input.h"
#include "log.h"
#include "net_http.h"
#include "playback.h"
#include "sync_net.h"
#include "version.h"

#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Build tag (WHIPFW:<board>:<ver>:<api>;) lives in fw_tag.cpp.
extern "C" const char kWhipFwTag[];

// Hold off the core's boot-time "mark valid": Ota::service() decides once the
// network is up (see ota.h).
extern "C" bool verifyRollbackLater() { return true; }

namespace {

static constexpr char kPrefsNs[] = "ota";
static constexpr uint32_t kHealthyMs = 30000;
static constexpr uint32_t kGiveUpMs = 120000;
static constexpr uint8_t kMaxTries = 3;
static constexpr size_t kTagMax = 100;
static constexpr size_t kScanTail = 112;
static constexpr size_t kScanChunk = 1536;
static constexpr int kSlack = 4096;

enum class UpState : uint8_t { Idle, Receiving, Done, Error };

static bool s_pending = false;
static bool s_rolledBack = false;

static UpState s_up = UpState::Idle;
static const char *s_err = nullptr;
static uint32_t s_bytes = 0;
static uint32_t s_total = 0;
static bool s_tagFound = false;
static char s_tagBoard[40];
static char s_tagVer[24];
static uint8_t s_scan[kScanTail + kScanChunk];
static size_t s_scanLen = 0;

// ---------------------------------------------------------------- helpers

static void appendEsc(String &out, const char *s) {
  out += '"';
  for (; s && *s; ++s) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

// Numeric compare of dotted versions: <0 when a is older than b.
static int cmpVer(const char *a, const char *b) {
  for (int part = 0; part < 3; ++part) {
    const long x = strtol(a, const_cast<char **>(&a), 10);
    const long y = strtol(b, const_cast<char **>(&b), 10);
    if (x != y) {
      return x < y ? -1 : 1;
    }
    if (*a == '.') {
      ++a;
    }
    if (*b == '.') {
      ++b;
    }
  }
  return 0;
}

static bool tagChar(uint8_t c) {
  return isalnum(c) || c == '-' || c == '.' || c == '_';
}

// p follows "WHIPFW:". Needs "<board>:<ver>:<api>;" of tag characters.
static bool parseTag(const uint8_t *p, size_t n) {
  char fields[3][40];
  size_t lens[3] = {0, 0, 0};
  uint8_t f = 0;
  for (size_t i = 0; i < n && i < kTagMax; ++i) {
    const uint8_t c = p[i];
    if (c == ';') {
      if (f != 2 || !lens[0] || !lens[1] || !lens[2]) {
        return false;
      }
      for (size_t k = 0; k < lens[2]; ++k) {
        if (!isdigit(static_cast<uint8_t>(fields[2][k]))) {
          return false;
        }
      }
      fields[0][lens[0]] = '\0';
      fields[1][lens[1]] = '\0';
      snprintf(s_tagBoard, sizeof(s_tagBoard), "%s", fields[0]);
      snprintf(s_tagVer, sizeof(s_tagVer), "%s", fields[1]);
      return true;
    }
    if (c == ':') {
      if (++f > 2) {
        return false;
      }
      continue;
    }
    if (!tagChar(c) || lens[f] + 1 >= sizeof(fields[f])) {
      return false;
    }
    fields[f][lens[f]++] = static_cast<char>(c);
  }
  return false;
}

// Find the build tag in the upload stream; it may straddle two chunks.
static void scanTag(const uint8_t *d, size_t n) {
  static const char kNeedle[7] = {'W', 'H', 'I', 'P', 'F', 'W', ':'};
  while (n && !s_tagFound) {
    size_t take = sizeof(s_scan) - s_scanLen;
    if (take > n) {
      take = n;
    }
    memcpy(s_scan + s_scanLen, d, take);
    d += take;
    n -= take;
    const size_t len = s_scanLen + take;
    for (size_t i = 0; i + sizeof(kNeedle) <= len; ++i) {
      if (s_scan[i] == 'W' && memcmp(s_scan + i, kNeedle, sizeof(kNeedle)) == 0 &&
          parseTag(s_scan + i + sizeof(kNeedle), len - i - sizeof(kNeedle))) {
        s_tagFound = true;
        return;
      }
    }
    const size_t keep = len < kScanTail ? len : kScanTail;
    memmove(s_scan, s_scan + len - keep, keep);
    s_scanLen = keep;
  }
}

static void rollback(const char *why) {
  LOG_V("ota", "rollback: %s", why);
  Preferences p;
  char prev[17] = {};
  if (p.begin(kPrefsNs, false)) {
    p.getString("prev", prev, sizeof(prev));
    p.putUChar("pending", 0);
    p.putUChar("rb", 1);
    p.end();
  }
  // Bootloader rollback (pending-verify image); returns only on failure.
  esp_ota_mark_app_invalid_rollback_and_reboot();
  const esp_partition_t *to =
      prev[0] ? esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev)
              : nullptr;
  if (to && esp_ota_set_boot_partition(to) == ESP_OK) {
    ESP.restart();
  }
  LOG_V("ota", "rollback impossible, keeping this image");
  s_pending = false;
}

// ---------------------------------------------------------------- peers

static constexpr uint8_t kMaxPeers = 12;
static constexpr size_t kStatusMax = 20480;
static constexpr uint16_t kCopyBlock = 4096;
static constexpr uint32_t kVerifyMs = 150000;

enum class PeerSt : uint8_t {
  Queued,
  Sending,
  Rebooting,
  Done,
  Current,
  OtherBoard,
  NeedsUsb,
  Busy,
  Failed,
  NoReply,
};
static const char *const kPeerStNames[] = {"queued",  "sending",   "rebooting", "done",
                                           "current", "other board", "needs usb", "busy",
                                           "failed",  "no reply"};

struct PeerRow {
  IPAddress ip;
  char name[24];
  char from[16];
  PeerSt st;
};

enum class PeersState : uint8_t { Idle, Running, Done, Error };
static const char *const kPeersNames[] = {"idle", "running", "done", "error"};

static volatile PeersState s_peers = PeersState::Idle;
static PeerRow s_rows[kMaxPeers];
static uint8_t s_rowN = 0;
static bool s_peersForce = false;
static char s_peersMsg[48];
static uint32_t s_peerSent = 0;
static uint32_t s_peerTotal = 0;

static bool sendImage(const IPAddress &ip, const esp_partition_t *part, uint32_t size) {
  WiFiClient c;
  if (!c.connect(ip, 80, NetHttp::kTimeoutMs)) {
    return false;
  }
  const char *bnd = "----whipota";
  const String head = String("--") + bnd +
                      "\r\nContent-Disposition: form-data; name=\"firmware\"; "
                      "filename=\"firmware.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  const String tail = String("\r\n--") + bnd + "--\r\n";
  const uint32_t total = head.length() + size + tail.length();
  c.printf("POST /ota?force=%d HTTP/1.1\r\nHost: %s\r\nContent-Type: "
           "multipart/form-data; boundary=%s\r\nContent-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           s_peersForce ? 1 : 0, ip.toString().c_str(), bnd, static_cast<unsigned>(total));
  c.print(head);
  uint8_t *buf = static_cast<uint8_t *>(malloc(kCopyBlock));
  if (!buf) {
    c.stop();
    return false;
  }
  bool ok = true;
  s_peerSent = 0;
  for (uint32_t off = 0; ok && off < size;) {
    const uint32_t want = size - off < kCopyBlock ? size - off : kCopyBlock;
    if (esp_partition_read(part, off, buf, want) != ESP_OK) {
      ok = false;
      break;
    }
    size_t w = 0;
    const uint32_t t0 = millis();
    while (w < want && millis() - t0 < NetHttp::kTimeoutMs) {
      const size_t n = c.write(buf + w, want - w);
      if (n == 0) {
        if (!c.connected()) {
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      w += n;
    }
    if (w != want) {
      ok = false;
      break;
    }
    off += want;
    s_peerSent = off;
  }
  free(buf);
  if (ok) {
    c.print(tail);
    ok = NetHttp::readStatusCode(c) == 200;
  }
  c.stop();
  return ok;
}

static void finishPeers(PeersState st, const char *msg) {
  snprintf(s_peersMsg, sizeof(s_peersMsg), "%s", msg);
  s_peers = st;
  LOG_V("ota", "peers %s", msg);
}

static void peersTask(void *) {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_image_metadata_t meta = {};
  const esp_partition_pos_t pos = {run->address, run->size};
  if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) != ESP_OK || meta.image_len == 0) {
    finishPeers(PeersState::Error, "own image unreadable");
    vTaskDelete(nullptr);
    return;
  }
  s_peerTotal = meta.image_len;
  char *body = static_cast<char *>(psramFound() ? ps_malloc(kStatusMax) : malloc(kStatusMax));
  if (!body) {
    finishPeers(PeersState::Error, "no memory");
    vTaskDelete(nullptr);
    return;
  }

  // Survey: which peers need this image.
  for (uint8_t i = 0; i < s_rowN; ++i) {
    PeerRow &r = s_rows[i];
    size_t len = 0;
    if (!NetHttp::get(r.ip, "/status", body, kStatusMax, len)) {
      r.st = PeerSt::NoReply;
      continue;
    }
    const char *end = body + len;
    char board[40] = {};
    NetHttp::jsonStr(body, end, "board", board, sizeof(board));
    NetHttp::jsonStr(body, end, "ver", r.from, sizeof(r.from));
    const long api = NetHttp::jsonInt(body, end, "api", 0);
    const char *ota = NetHttp::findKey(body, end, "ota");
    const bool busy = ota && NetHttp::jsonBool(ota, end, "busy");
    if (strcmp(board, BoardProfile::id()) != 0) {
      r.st = PeerSt::OtherBoard;
    } else if (cmpVer(r.from, kFirmwareVersion) >= 0) {
      r.st = PeerSt::Current;
    } else if (api < 3 || !ota) {
      r.st = PeerSt::NeedsUsb;
    } else if (busy && !s_peersForce) {
      r.st = PeerSt::Busy;
    } else {
      r.st = PeerSt::Queued;
    }
  }

  // Send, one peer at a time; they reboot while the next one uploads.
  uint8_t sent = 0;
  uint32_t lastSend = millis();
  for (uint8_t i = 0; i < s_rowN; ++i) {
    PeerRow &r = s_rows[i];
    if (r.st != PeerSt::Queued) {
      continue;
    }
    r.st = PeerSt::Sending;
    if (sendImage(r.ip, run, meta.image_len)) {
      r.st = PeerSt::Rebooting;
      ++sent;
      lastSend = millis();
    } else {
      r.st = PeerSt::Failed;
    }
  }

  // Verify: each updated peer comes back reporting this version.
  while (sent && millis() - lastSend < kVerifyMs) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    uint8_t waiting = 0;
    for (uint8_t i = 0; i < s_rowN; ++i) {
      PeerRow &r = s_rows[i];
      if (r.st != PeerSt::Rebooting) {
        continue;
      }
      size_t len = 0;
      char ver[16] = {};
      if (NetHttp::get(r.ip, "/status", body, kStatusMax, len) &&
          NetHttp::jsonStr(body, body + len, "ver", ver, sizeof(ver)) &&
          strcmp(ver, kFirmwareVersion) == 0) {
        r.st = PeerSt::Done;
      } else {
        ++waiting;
      }
    }
    if (!waiting) {
      break;
    }
  }
  uint8_t done = 0;
  uint8_t failed = 0;
  for (uint8_t i = 0; i < s_rowN; ++i) {
    if (s_rows[i].st == PeerSt::Rebooting) {
      s_rows[i].st = PeerSt::NoReply;
    }
    done += s_rows[i].st == PeerSt::Done;
    failed += s_rows[i].st == PeerSt::Failed || s_rows[i].st == PeerSt::NoReply;
  }
  free(body);
  char msg[48];
  snprintf(msg, sizeof(msg), "%u updated, %u failed", done, failed);
  finishPeers(PeersState::Done, msg);
  vTaskDelete(nullptr);
}

} // namespace

// ================================================================== boot

void Ota::bootGuard() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  const bool pendingVerify =
      run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Preferences p;
  if (!p.begin(kPrefsNs, false)) {
    s_pending = pendingVerify;
    return;
  }
  s_rolledBack = p.getUChar("rb", 0) != 0;
  if (p.getUChar("pending", 0)) {
    char prev[17] = {};
    p.getString("prev", prev, sizeof(prev));
    if (run && strcmp(prev, run->label) == 0) {
      // The bootloader already went back to the previous image.
      p.putUChar("pending", 0);
      p.putUChar("rb", 1);
      s_rolledBack = true;
      LOG_V("ota", "new image failed; rolled back to %s", run->label);
    } else {
      const uint8_t tries = p.getUChar("tries", 0) + 1;
      p.putUChar("tries", tries);
      s_pending = true;
      LOG_V("ota", "trial boot %u of new image", tries);
      if (tries > kMaxTries) {
        p.end();
        rollback("crash loop");
        return;
      }
    }
  }
  p.end();
  s_pending = s_pending || pendingVerify;
  LOG_V("ota", "%s on %s", kWhipFwTag, run ? run->label : "?");
}

void Ota::service(bool netUp) {
  if (!s_pending) {
    return;
  }
  const uint32_t now = millis();
  if (netUp && now >= kHealthyMs) {
    esp_ota_mark_app_valid_cancel_rollback();
    Preferences p;
    if (p.begin(kPrefsNs, false)) {
      p.putUChar("pending", 0);
      p.putUChar("tries", 0);
      p.end();
    }
    s_pending = false;
    LOG_V("ota", "new image healthy, kept");
  } else if (now >= kGiveUpMs) {
    rollback("not healthy");
  }
}

bool Ota::busy() {
  return (Playback::playing() && !Playback::userPaused()) || LiveInput::active() ||
         Distribute::running() || StreamTx::running();
}

// ================================================================== upload

void Ota::uploadStart(int contentLength, bool force) {
  s_err = nullptr;
  s_bytes = 0;
  s_total = contentLength > 0 ? static_cast<uint32_t>(contentLength) : 0;
  s_tagFound = false;
  s_tagBoard[0] = '\0';
  s_tagVer[0] = '\0';
  s_scanLen = 0;
  s_up = UpState::Receiving;
  if (Update.isRunning()) {
    Update.abort();
  }
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    s_err = "no ota slot";
  } else if (peersRunning()) {
    s_err = "busy";
  } else if (!force && busy()) {
    s_err = "busy";
  } else if (contentLength > 0 && contentLength > static_cast<int>(next->size) + kSlack) {
    s_err = "too large";
  }
  if (s_err) {
    s_up = UpState::Error;
    return;
  }
  Playback::park();
  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    s_err = "begin failed";
    s_up = UpState::Error;
    return;
  }
  LOG_V("ota", "receiving into %s (%u bytes)", next->label,
        static_cast<unsigned>(s_total));
}

void Ota::uploadWrite(const uint8_t *data, size_t len) {
  if (s_err || s_up != UpState::Receiving) {
    return;
  }
  scanTag(data, len);
  if (Update.write(const_cast<uint8_t *>(data), len) != len) {
    s_err = s_bytes == 0 ? "not firmware" : (Update.getError() == UPDATE_ERROR_SPACE ? "too large" : "write failed");
    Update.abort();
    s_up = UpState::Error;
    return;
  }
  s_bytes += len;
}

void Ota::uploadEnd(bool aborted) {
  if (s_up != UpState::Receiving || s_err) {
    if (!s_err) {
      s_err = "no file";
    }
    s_up = UpState::Error;
    return;
  }
  if (aborted) {
    s_err = "aborted";
  } else if (!s_tagFound) {
    s_err = "not a whip image";
  } else if (strcmp(s_tagBoard, BoardProfile::id()) != 0) {
    s_err = "other board";
  }
  if (s_err) {
    Update.abort();
    s_up = UpState::Error;
    LOG_V("ota", "rejected: %s", s_err);
    return;
  }
  if (!Update.end(true)) {
    s_err = "verify failed";
    s_up = UpState::Error;
    LOG_V("ota", "verify failed (%u)", static_cast<unsigned>(Update.getError()));
    return;
  }
  const esp_partition_t *run = esp_ota_get_running_partition();
  Preferences p;
  if (p.begin(kPrefsNs, false)) {
    p.putUChar("pending", 1);
    p.putUChar("tries", 0);
    p.putUChar("rb", 0);
    p.putString("prev", run ? run->label : "");
    p.end();
  }
  s_rolledBack = false;
  s_up = UpState::Done;
  LOG_V("ota", "written %s (%u bytes); rebooting", s_tagVer, static_cast<unsigned>(s_bytes));
}

const char *Ota::uploadError() { return s_err; }

const char *Ota::uploadVersion() { return s_tagVer; }

bool Ota::uploadOk() { return s_up == UpState::Done && !s_err; }

// ================================================================== peers

bool Ota::peersRunning() { return s_peers == PeersState::Running; }

bool Ota::startPeers(bool force, const char *&error) {
  error = nullptr;
  if (peersRunning()) {
    error = "busy";
    return false;
  }
  if (Distribute::running()) {
    error = "distributing";
    return false;
  }
  if (StreamTx::running()) {
    error = "streaming";
    return false;
  }
  if (s_pending) {
    // Never spread an image that has not proven itself here yet.
    error = "unverified";
    return false;
  }
  s_rowN = 0;
  for (uint8_t i = 0; i < SyncNet::peerCount() && s_rowN < kMaxPeers; ++i) {
    const SyncPeer *peer = SyncNet::peerAt(i);
    if (peer->role == static_cast<uint8_t>(SyncRole::Companion)) {
      continue;
    }
    PeerRow &r = s_rows[s_rowN++];
    r.ip = peer->ip;
    snprintf(r.name, sizeof(r.name), "%s", peer->name);
    r.from[0] = '\0';
    r.st = PeerSt::Queued;
  }
  if (s_rowN == 0) {
    error = "no peers";
    return false;
  }
  s_peersForce = force;
  s_peersMsg[0] = '\0';
  s_peerSent = 0;
  s_peerTotal = 0;
  s_peers = PeersState::Running;
  if (xTaskCreate(peersTask, "otapeers", 8192, nullptr, 1, nullptr) != pdPASS) {
    s_peers = PeersState::Error;
    error = "task";
    return false;
  }
  LOG_V("ota", "updating up to %u peers", s_rowN);
  return true;
}

// ================================================================== status

void Ota::appendStatus(String &out) {
  static const char *const kUpNames[] = {"idle", "receiving", "done", "error"};
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  out += "\"ota\":{\"max\":";
  out += next ? static_cast<unsigned>(next->size) : 0u;
  out += ",\"state\":\"";
  out += kUpNames[static_cast<uint8_t>(s_up)];
  out += "\",\"bytes\":";
  out += static_cast<unsigned>(s_bytes);
  out += ",\"total\":";
  out += static_cast<unsigned>(s_total);
  out += ",\"err\":";
  appendEsc(out, s_err ? s_err : "");
  out += ",\"pending\":";
  out += s_pending ? "true" : "false";
  out += ",\"rolled_back\":";
  out += s_rolledBack ? "true" : "false";
  out += ",\"busy\":";
  out += busy() ? "true" : "false";
  out += ",\"tag\":";
  appendEsc(out, kWhipFwTag);
  out += "},\"ota_peers\":{\"state\":\"";
  out += kPeersNames[static_cast<uint8_t>(s_peers)];
  out += "\",\"msg\":";
  appendEsc(out, s_peersMsg);
  out += ",\"sent\":";
  out += static_cast<unsigned>(s_peerSent);
  out += ",\"total\":";
  out += static_cast<unsigned>(s_peerTotal);
  out += ",\"rows\":[";
  if (s_peers != PeersState::Idle) {
    for (uint8_t i = 0; i < s_rowN; ++i) {
      if (i) {
        out += ',';
      }
      out += "{\"name\":";
      appendEsc(out, s_rows[i].name);
      out += ",\"ip\":\"";
      out += s_rows[i].ip.toString();
      out += "\",\"from\":";
      appendEsc(out, s_rows[i].from);
      out += ",\"st\":\"";
      out += kPeerStNames[static_cast<uint8_t>(s_rows[i].st)];
      out += "\"}";
    }
  }
  out += "]}";
}
