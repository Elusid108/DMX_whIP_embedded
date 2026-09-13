#include "wifi_setup.h"

#include "identify.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "node_id.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "version.h"
#include "wifi_setup_html.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {

enum class ConnectStatus : uint8_t {
  Idle = 0,
  Connecting,
  Connected,
  Failed,
};

static constexpr uint16_t kHttpPort = 80;
static constexpr uint16_t kDnsPort = 53;
static constexpr uint32_t kConnectTimeoutMs = 20000;
static constexpr uint32_t kScanDwellMs = 75;
static constexpr uint32_t kDisconnectGraceMs = 800;
static constexpr int kMaxNets = 24;
static constexpr char kPrefsNs[] = "wifi";

static WebServer s_server(kHttpPort);
static DNSServer s_dns;
static String s_pendingSsid;
static String s_savedSsid;
static char s_error[48];
static ConnectStatus s_connectStatus = ConnectStatus::Idle;
static bool s_scanRunning = false;
static bool s_haveScan = false;
static uint32_t s_connectStart = 0;
static bool s_apUp = false;
static volatile bool s_gotIp = false;
static volatile bool s_discPending = false;
static volatile bool s_lostPending = false;
static volatile uint8_t s_discReason = 0;

static File s_uploadFile;
static char s_uploadPath[kSdPathLen];
static uint32_t s_uploadBytes = 0;
static bool s_uploadOk = false;
static bool s_uploadLocked = false;
static const char *s_uploadError = nullptr;

static const char *stateName() {
  if (s_scanRunning) {
    return "scanning";
  }
  switch (s_connectStatus) {
  case ConnectStatus::Connecting:
    return "connecting";
  case ConnectStatus::Connected:
    return "connected";
  case ConnectStatus::Failed:
    return "failed";
  case ConnectStatus::Idle:
  default:
    return "idle";
  }
}

static void jsonEscape(String &out, const String &s) {
  out += '"';
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      continue;
    } else {
      out += c;
    }
  }
  out += '"';
}

static void sendJson(int code, const String &body) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json", body);
}

static bool portalParked() {
  return LiveInput::active() && LiveCfg::park();
}

static void sendPage() {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("Connection", "close");
  s_server.send_P(200, "text/html", kWifiSetupHtml);
}

static void handleCaptive() {
  LOG_V("http", "captive %s", s_server.uri().c_str());
  sendPage();
}

static const char *reasonText(uint8_t reason) {
  switch (reason) {
  case WIFI_REASON_NO_AP_FOUND:
    return "no network found";
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "auth failed";
  default:
    return nullptr;
  }
}

static void setError(const char *text) {
  strncpy(s_error, text != nullptr ? text : "failed", sizeof(s_error) - 1);
  s_error[sizeof(s_error) - 1] = '\0';
}

static void saveCreds(const String &ssid, const String &pass) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("wifi", "nvs open failed");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  s_savedSsid = ssid;
  LOG_V("wifi", "saved ssid=%s", ssid.c_str());
}

static void clearCreds() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("wifi", "nvs open failed");
    return;
  }
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  s_savedSsid = "";
  LOG_V("wifi", "forgot saved network");
}

static void startAp() {
  if (s_apUp) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(kApIp, kApIp, kApMask)) {
    LOG_C("ap", "softAPConfig failed");
  }
  if (!WiFi.softAP(kApSsid, kApPass)) {
    LOG_C("ap", "softAP failed");
    return;
  }
  s_dns.setTTL(0);
  if (!s_dns.start(kDnsPort, "*", kApIp)) {
    LOG_C("ap", "dns failed");
  }
  s_apUp = true;
  LOG_V("ap", "up (idle) ssid=%s ip=%s", kApSsid,
        WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!s_apUp) {
    return;
  }
  s_dns.stop();
  WiFi.softAPdisconnect(false);
  s_apUp = false;
  LOG_V("ap", "down (live)");
}

static void failConnect(const char *why) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  s_connectStatus = ConnectStatus::Failed;
  setError(why);
  LOG_C("wifi", "connect failed ssid=%s %s", s_pendingSsid.c_str(), s_error);
  startAp();
}

static void beginConnect(const String &ssid, const String &pass) {
  s_pendingSsid = ssid;
  s_error[0] = '\0';
  s_gotIp = false;
  s_discPending = false;
  s_lostPending = false;
  s_connectStart = millis();
  s_connectStatus = ConnectStatus::Connecting;
  WiFi.setAutoReconnect(true);
  if (pass.length() == 0) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  LOG_V("wifi", "connect ssid=%s", ssid.c_str());
}

static void startScan() {
  WiFi.scanDelete();
  s_haveScan = false;
  const int16_t rc =
      WiFi.scanNetworks(true, false, true, kScanDwellMs);
  if (rc == WIFI_SCAN_FAILED) {
    LOG_C("wifi", "scan start failed");
    s_scanRunning = false;
    return;
  }
  s_scanRunning = true;
  LOG_V("wifi", "scan start");
}

static void sendStatus(int code) {
  String out;
  out.reserve(4096);
  out += "{\"state\":\"";
  out += stateName();
  out += "\",\"ver\":";
  jsonEscape(out, String(kFirmwareVersion));
  out += ",\"name\":";
  jsonEscape(out, String(NodeId::longName()));
  out += ",\"short\":";
  jsonEscape(out, String(NodeId::shortName()));
  String ssid = s_pendingSsid;
  if (ssid.isEmpty() && WiFi.status() == WL_CONNECTED) {
    ssid = WiFi.SSID();
  }
  if (ssid.length()) {
    out += ",\"ssid\":";
    jsonEscape(out, ssid);
  }
  if (s_savedSsid.length()) {
    out += ",\"saved\":";
    jsonEscape(out, s_savedSsid);
  }
  if (WiFi.status() == WL_CONNECTED) {
    out += ",\"ip\":\"";
    out += WiFi.localIP().toString();
    out += '"';
  }
  out += ",\"ap_ip\":\"";
  out += WiFi.softAPIP().toString();
  out += '"';
  if (s_connectStatus == ConnectStatus::Failed && s_error[0] != '\0') {
    out += ",\"error\":";
    jsonEscape(out, String(s_error));
  }
  out += ",\"bri\":";
  out += static_cast<unsigned>(LedCtrl::get());
  out += ",\"sd\":{\"ok\":";
  out += SdInfo::ok() ? "true" : "false";
  if (SdInfo::ok()) {
    out += ",\"type\":";
    jsonEscape(out, String(SdInfo::type()));
    out += ",\"size_mb\":";
    out += SdInfo::sizeMb();
    out += ",\"used_mb\":";
    out += SdInfo::usedMb();
    out += ",\"free_mb\":";
    out += SdInfo::freeMb();
  }
  out += '}';
  out += ",\"proto\":";
  jsonEscape(out, String(LiveCfg::protoName()));
  out += ",\"fps\":";
  out += static_cast<unsigned>(LiveCfg::fps());
  out += ",\"buf\":";
  out += static_cast<unsigned>(LiveCfg::buf());
  out += ",\"park\":";
  jsonEscape(out, String(LiveCfg::parkName()));
  out += ",\"live\":";
  out += LiveInput::active() ? "true" : "false";
  out += ",\"play\":{\"src\":";
  jsonEscape(out, String(PlayCfg::srcName()));
  out += ",\"path\":";
  jsonEscape(out, String(PlayCfg::path()));
  out += ",\"file_loop\":";
  jsonEscape(out, String(PlayCfg::fileLoopName()));
  out += ",\"folder_rep\":";
  jsonEscape(out, String(PlayCfg::folderRepName()));
  out += ",\"n\":";
  out += static_cast<unsigned>(PlayCfg::folderN());
  out += ",\"now\":";
  jsonEscape(out, String(Playback::parked() ? "" : Playback::path()));
  out += ",\"files\":[";
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::fileAt(i)));
  }
  out += "],\"dirs\":[";
  for (uint8_t i = 0; i < SdInfo::dirCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::dirAt(i)));
  }
  out += "]}}";
  sendJson(code, out);
}

static void sendScanResults() {
  String out;
  out.reserve(1024);
  out += "{\"state\":\"idle\",\"networks\":[";
  bool first = true;
  int emitted = 0;
  const int16_t n = WiFi.scanComplete();
  if (n > 0) {
    for (int i = 0; i < n && emitted < kMaxNets; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) {
        continue;
      }
      if (!first) {
        out += ',';
      }
      first = false;
      out += "{\"ssid\":";
      jsonEscape(out, ssid);
      out += ",\"rssi\":";
      out += WiFi.RSSI(i);
      out += ",\"secure\":";
      out += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
      out += '}';
      ++emitted;
    }
  }
  out += "]}";
  sendJson(200, out);
}

static void handleScan() {
  if (s_connectStatus == ConnectStatus::Connecting) {
    sendJson(200, "{\"state\":\"connecting\"}");
    return;
  }
  const bool force = s_server.hasArg("start");
  if (s_scanRunning) {
    sendJson(200, "{\"state\":\"scanning\"}");
    return;
  }
  if (force || !s_haveScan) {
    startScan();
    sendJson(200, "{\"state\":\"scanning\"}");
    return;
  }
  sendScanResults();
}

static void handleStatus() { sendStatus(200); }

static void handleIdentify() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  uint32_t ms = 3000;
  if (s_server.hasArg("ms")) {
    const String arg = s_server.arg("ms");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 200 || v > 15000) {
      sendJson(400, "{\"error\":\"bad ms\"}");
      return;
    }
    ms = static_cast<uint32_t>(v);
  }
  Identify::start(ms);
  String out = "{\"identify\":true,\"ms\":";
  out += static_cast<unsigned>(ms);
  out += '}';
  sendJson(200, out);
}

static void handleBrightness() {
  if (portalParked()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!s_server.hasArg("v")) {
    sendJson(400, "{\"error\":\"bad v\"}");
    return;
  }
  const String arg = s_server.arg("v");
  char *end = nullptr;
  const long v = strtol(arg.c_str(), &end, 10);
  if (end == arg.c_str() || *end != '\0' || v < 0 || v > 255) {
    sendJson(400, "{\"error\":\"bad v\"}");
    return;
  }
  LedCtrl::set(static_cast<uint8_t>(v), true);
  sendStatus(200);
}

static void handleLive() {
  if (portalParked()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  const String protoArg = s_server.arg("proto");
  LiveProto proto = LiveCfg::proto();
  if (protoArg == "auto") {
    proto = LiveProto::Auto;
  } else if (protoArg == "artnet") {
    proto = LiveProto::ArtNet;
  } else if (protoArg == "sacn") {
    proto = LiveProto::Sacn;
  } else if (protoArg.length()) {
    sendJson(400, "{\"error\":\"bad proto\"}");
    return;
  }

  char *end = nullptr;
  uint8_t fps = LiveCfg::fps();
  if (s_server.hasArg("fps")) {
    const String fpsArg = s_server.arg("fps");
    const long v = strtol(fpsArg.c_str(), &end, 10);
    if (end == fpsArg.c_str() || *end != '\0' || v < 0 || v > 255) {
      sendJson(400, "{\"error\":\"bad fps\"}");
      return;
    }
    fps = static_cast<uint8_t>(v);
  }

  uint8_t buf = LiveCfg::buf();
  if (s_server.hasArg("buf")) {
    const String bufArg = s_server.arg("buf");
    const long v = strtol(bufArg.c_str(), &end, 10);
    if (end == bufArg.c_str() || *end != '\0' || v < 0 || v > 3) {
      sendJson(400, "{\"error\":\"bad buf\"}");
      return;
    }
    buf = static_cast<uint8_t>(v);
  }

  bool park = LiveCfg::park();
  if (s_server.hasArg("park")) {
    const String parkArg = s_server.arg("park");
    if (parkArg == "yes") {
      park = true;
    } else if (parkArg == "no") {
      park = false;
    } else {
      sendJson(400, "{\"error\":\"bad park\"}");
      return;
    }
  }

  if (!LiveCfg::set(proto, fps, buf, park, true)) {
    sendJson(400, "{\"error\":\"bad live\"}");
    return;
  }
  sendStatus(200);
}

static bool endsWithDmx(const char *p) {
  const size_t n = p ? strlen(p) : 0;
  return n >= 4 && strcasecmp(p + n - 4, ".dmx") == 0;
}

static bool validUploadPath(const char *p) {
  if (!p || p[0] != '/' || strstr(p, "..") != nullptr) {
    return false;
  }
  const size_t n = strlen(p);
  if (n < 6 || n >= kSdPathLen || p[n - 1] == '/') {
    return false;
  }
  return endsWithDmx(p);
}

static const char *fileBase(const char *path) {
  const char *slash = path ? strrchr(path, '/') : nullptr;
  return slash ? slash + 1 : path;
}

static void stripOrderPrefix(const char *base, char *out, size_t outLen) {
  const char *src = base && base[0] ? base : "";
  if (isdigit(static_cast<unsigned char>(src[0])) &&
      isdigit(static_cast<unsigned char>(src[1])) && src[2] == '_') {
    src += 3;
  }
  snprintf(out, outLen, "%s", src);
}

static void remapPlayPath(const char *from, const char *to) {
  if (!from || !to || strcmp(from, to) == 0) {
    return;
  }
  if (PlayCfg::src() != PlaySrc::File || strcmp(PlayCfg::path(), from) != 0) {
    return;
  }
  PlayCfg::set(PlaySrc::File, to, PlayCfg::fileLoop(), PlayCfg::folderRep(),
               PlayCfg::folderN(), true);
}

static void resetUploadState() {
  s_uploadOk = false;
  s_uploadBytes = 0;
  s_uploadError = nullptr;
  s_uploadPath[0] = '\0';
}

static void closeUploadFile() {
  if (s_uploadFile) {
    s_uploadFile.close();
  }
}

static void releaseUploadLock() {
  if (s_uploadLocked) {
    SdInfo::unlock();
    s_uploadLocked = false;
  }
}

static void handleUploadFile() {
  HTTPUpload &up = s_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    resetUploadState();
    if (LiveInput::active()) {
      s_uploadError = "live";
      return;
    }
    if (!SdInfo::ok()) {
      s_uploadError = "no sd";
      return;
    }
    String path = s_server.arg("path");
    path.trim();
    if (!path.length() && up.filename.length()) {
      path = "/";
      path += up.filename;
    }
    if (!validUploadPath(path.c_str())) {
      s_uploadError = "bad path";
      return;
    }
    snprintf(s_uploadPath, sizeof(s_uploadPath), "%s", path.c_str());
    Playback::park();
    delay(80);
    if (!SdInfo::lock(2000)) {
      s_uploadError = "busy";
      return;
    }
    s_uploadLocked = true;
    s_uploadFile = SD.open(s_uploadPath, FILE_WRITE);
    if (!s_uploadFile) {
      releaseUploadLock();
      s_uploadError = "write failed";
      return;
    }
    LOG_V("http", "upload start %s", s_uploadPath);
    return;
  }
  if (up.status == UPLOAD_FILE_WRITE) {
    if (s_uploadError || !s_uploadFile) {
      return;
    }
    if (s_uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      s_uploadError = "write failed";
    } else {
      s_uploadBytes += up.currentSize;
    }
    yield();
    return;
  }
  if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    closeUploadFile();
    const bool aborted = up.status == UPLOAD_FILE_ABORTED;
    if (aborted && !s_uploadError) {
      s_uploadError = "aborted";
    }
    if (s_uploadPath[0] && s_uploadError &&
        (strcmp(s_uploadError, "write failed") == 0 ||
         strcmp(s_uploadError, "aborted") == 0)) {
      SD.remove(s_uploadPath);
    }
    releaseUploadLock();
    if (!s_uploadError && !aborted) {
      SdInfo::refreshTree();
      s_uploadOk = true;
      LOG_V("http", "upload ok %s bytes=%u", s_uploadPath,
            static_cast<unsigned>(s_uploadBytes));
    }
  }
}

static void handleUploadDone() {
  if (s_uploadError) {
    if (strcmp(s_uploadError, "live") == 0) {
      sendJson(503, "{\"error\":\"live\"}");
    } else if (strcmp(s_uploadError, "no sd") == 0) {
      sendJson(503, "{\"error\":\"no sd\"}");
    } else if (strcmp(s_uploadError, "busy") == 0) {
      sendJson(503, "{\"error\":\"busy\"}");
    } else if (strcmp(s_uploadError, "write failed") == 0) {
      sendJson(500, "{\"error\":\"write failed\"}");
    } else if (strcmp(s_uploadError, "aborted") == 0) {
      sendJson(400, "{\"error\":\"aborted\"}");
    } else {
      sendJson(400, "{\"error\":\"bad path\"}");
    }
    return;
  }
  if (!s_uploadOk) {
    sendJson(400, "{\"error\":\"no file\"}");
    return;
  }
  String out = "{\"ok\":true,\"path\":";
  jsonEscape(out, String(s_uploadPath));
  out += ",\"bytes\":";
  out += s_uploadBytes;
  out += '}';
  sendJson(200, out);
}

static void handleName() {
  if (portalParked()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  String longName = s_server.arg("long");
  longName.trim();
  String shortName = s_server.arg("short");
  shortName.trim();
  if (!NodeId::set(longName.c_str(),
                   shortName.length() ? shortName.c_str() : nullptr, true)) {
    sendJson(400, "{\"error\":\"bad name\"}");
    return;
  }
  sendStatus(200);
}

static void handleRename() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!SdInfo::ok()) {
    sendJson(503, "{\"error\":\"no sd\"}");
    return;
  }
  String from = s_server.arg("from");
  String to = s_server.arg("to");
  from.trim();
  to.trim();
  if (!validUploadPath(from.c_str()) || !validUploadPath(to.c_str())) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  if (from == to) {
    sendStatus(200);
    return;
  }
  Playback::park();
  delay(80);
  if (!SdInfo::lock(2000)) {
    sendJson(503, "{\"error\":\"busy\"}");
    return;
  }
  if (!SD.exists(from.c_str())) {
    SdInfo::unlock();
    sendJson(404, "{\"error\":\"missing\"}");
    return;
  }
  if (SD.exists(to.c_str())) {
    SdInfo::unlock();
    sendJson(409, "{\"error\":\"exists\"}");
    return;
  }
  const bool ok = SD.rename(from.c_str(), to.c_str());
  SdInfo::unlock();
  if (!ok) {
    sendJson(500, "{\"error\":\"rename failed\"}");
    return;
  }
  SdInfo::refreshTree();
  remapPlayPath(from.c_str(), to.c_str());
  sendStatus(200);
}

static void handleFileGet() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!SdInfo::ok()) {
    sendJson(503, "{\"error\":\"no sd\"}");
    return;
  }
  String path = s_server.arg("path");
  path.trim();
  if (!validUploadPath(path.c_str())) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  Playback::park();
  delay(80);
  if (!SdInfo::lock(2000)) {
    sendJson(503, "{\"error\":\"busy\"}");
    return;
  }
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) {
    SdInfo::unlock();
    sendJson(404, "{\"error\":\"missing\"}");
    return;
  }
  s_server.sendHeader("Cache-Control", "no-store");
  String disp = "attachment; filename=\"";
  disp += fileBase(path.c_str());
  disp += '"';
  s_server.sendHeader("Content-Disposition", disp);
  s_server.streamFile(f, "application/octet-stream");
  f.close();
  SdInfo::unlock();
}

static void handleOrder() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!SdInfo::ok()) {
    sendJson(503, "{\"error\":\"no sd\"}");
    return;
  }

  char froms[kSdMaxListFiles][kSdPathLen];
  char dests[kSdMaxListFiles][kSdPathLen];
  uint8_t n = 0;
  const int args = s_server.args();
  for (int i = 0; i < args && n < kSdMaxListFiles; ++i) {
    if (s_server.argName(i) != "path") {
      continue;
    }
    String p = s_server.arg(i);
    p.trim();
    if (!validUploadPath(p.c_str())) {
      sendJson(400, "{\"error\":\"bad path\"}");
      return;
    }
    snprintf(froms[n], kSdPathLen, "%s", p.c_str());
    char stripped[kSdPathLen];
    stripOrderPrefix(fileBase(froms[n]), stripped, sizeof(stripped));
    if (!stripped[0] || !endsWithDmx(stripped)) {
      sendJson(400, "{\"error\":\"bad path\"}");
      return;
    }
    const int wrote =
        snprintf(dests[n], kSdPathLen, "/%02u_%s", n + 1, stripped);
    if (wrote < 0 || wrote >= static_cast<int>(kSdPathLen)) {
      sendJson(400, "{\"error\":\"bad path\"}");
      return;
    }
    ++n;
  }
  if (n == 0) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < n; ++j) {
      if (strcmp(froms[i], froms[j]) == 0 || strcmp(dests[i], dests[j]) == 0) {
        sendJson(409, "{\"error\":\"exists\"}");
        return;
      }
    }
  }

  Playback::park();
  delay(80);
  if (!SdInfo::lock(4000)) {
    sendJson(503, "{\"error\":\"busy\"}");
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    if (!SD.exists(froms[i])) {
      SdInfo::unlock();
      sendJson(404, "{\"error\":\"missing\"}");
      return;
    }
  }
  bool ok = true;
  char temps[kSdMaxListFiles][kSdPathLen];
  for (uint8_t i = 0; i < n && ok; ++i) {
    snprintf(temps[i], kSdPathLen, "/@%02u.dmx", i + 1);
    if (strcmp(froms[i], temps[i]) == 0) {
      continue;
    }
    if (SD.exists(temps[i]) && !SD.remove(temps[i])) {
      ok = false;
      break;
    }
    ok = SD.rename(froms[i], temps[i]);
  }
  for (uint8_t i = 0; i < n && ok; ++i) {
    const char *src = SD.exists(temps[i]) ? temps[i] : froms[i];
    if (strcmp(src, dests[i]) == 0) {
      continue;
    }
    if (SD.exists(dests[i]) && !SD.remove(dests[i])) {
      ok = false;
      break;
    }
    ok = SD.rename(src, dests[i]);
  }
  SdInfo::unlock();
  if (!ok) {
    SdInfo::refreshTree();
    sendJson(500, "{\"error\":\"order failed\"}");
    return;
  }
  SdInfo::refreshTree();
  for (uint8_t i = 0; i < n; ++i) {
    remapPlayPath(froms[i], dests[i]);
  }
  sendStatus(200);
}

static void handlePlay() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  const String srcArg = s_server.arg("src");
  const String actionArg = s_server.arg("action");
  if (srcArg == "stop" || actionArg == "stop") {
    Playback::park();
    sendStatus(200);
    return;
  }

  PlaySrc src = PlayCfg::src();
  if (srcArg == "root") {
    src = PlaySrc::Root;
  } else if (srcArg == "file") {
    src = PlaySrc::File;
  } else if (srcArg == "folder") {
    src = PlaySrc::Folder;
  } else if (srcArg.length()) {
    sendJson(400, "{\"error\":\"bad src\"}");
    return;
  }

  const String pathArg = s_server.hasArg("path") ? s_server.arg("path")
                                                 : String(PlayCfg::path());

  PlayFileLoop fileLoop = PlayCfg::fileLoop();
  const String flpArg = s_server.arg("file_loop");
  if (flpArg == "one") {
    fileLoop = PlayFileLoop::One;
  } else if (flpArg == "all") {
    fileLoop = PlayFileLoop::All;
  } else if (flpArg.length()) {
    sendJson(400, "{\"error\":\"bad file_loop\"}");
    return;
  }

  PlayFolderRep folderRep = PlayCfg::folderRep();
  const String frpArg = s_server.arg("folder_rep");
  if (frpArg == "forever") {
    folderRep = PlayFolderRep::Forever;
  } else if (frpArg == "count") {
    folderRep = PlayFolderRep::Count;
  } else if (frpArg.length()) {
    sendJson(400, "{\"error\":\"bad folder_rep\"}");
    return;
  }

  uint8_t n = PlayCfg::folderN();
  if (s_server.hasArg("n")) {
    const String nArg = s_server.arg("n");
    char *end = nullptr;
    const long v = strtol(nArg.c_str(), &end, 10);
    if (end == nArg.c_str() || *end != '\0' || v < 1 || v > 99) {
      sendJson(400, "{\"error\":\"bad n\"}");
      return;
    }
    n = static_cast<uint8_t>(v);
  }

  if (!PlayCfg::set(src, pathArg.c_str(), fileLoop, folderRep, n, true)) {
    sendJson(400, "{\"error\":\"bad play\"}");
    return;
  }
  Playback::reload();
  sendStatus(200);
}

static void handleConnect() {
  if (portalParked()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (s_scanRunning) {
    sendJson(409, "{\"error\":\"scan in progress\"}");
    return;
  }
  String ssid = s_server.arg("ssid");
  ssid.trim();
  const String pass = s_server.arg("password");
  if (ssid.length() == 0 || ssid.length() > 32) {
    sendJson(400, "{\"error\":\"bad ssid\"}");
    return;
  }
  if (pass.length() > 0 && pass.length() < 8) {
    sendJson(400, "{\"error\":\"password must be 8+ characters\"}");
    return;
  }
  if (pass.length() > 63) {
    sendJson(400, "{\"error\":\"password too long\"}");
    return;
  }
  LOG_V("http", "connect ssid=%s", ssid.c_str());
  saveCreds(ssid, pass);
  beginConnect(ssid, pass);
  sendStatus(200);
}

static void handleForget() {
  if (portalParked()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  clearCreds();
  s_pendingSsid = "";
  s_connectStatus = ConnectStatus::Idle;
  s_error[0] = '\0';
  startAp();
  LOG_V("http", "forget");
  sendStatus(200);
}

static void handleNotFound() {
  if (s_server.uri() == "/favicon.ico") {
    s_server.send(204);
    return;
  }
  LOG_V("http", "captive %s", s_server.uri().c_str());
  sendPage();
}

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    s_gotIp = true;
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    s_discReason = reason;
    if (s_connectStatus == ConnectStatus::Connecting &&
        (millis() - s_connectStart) > kDisconnectGraceMs) {
      s_discPending = true;
    } else if (s_connectStatus == ConnectStatus::Connected) {
      s_lostPending = true;
    }
    break;
  }
  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    LOG_V("ap", "client join");
    break;
  default:
    break;
  }
}

static void pollScan() {
  if (!s_scanRunning) {
    return;
  }
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  s_scanRunning = false;
  if (n < 0) {
    LOG_C("wifi", "scan failed");
    s_haveScan = false;
    return;
  }
  s_haveScan = true;
  LOG_V("wifi", "scan n=%d", n);
}

static void pollConnect() {
  if (s_gotIp) {
    s_gotIp = false;
    s_connectStatus = ConnectStatus::Connected;
    s_error[0] = '\0';
    LOG_V("wifi", "connected ssid=%s ip=%s", WiFi.SSID().c_str(),
          WiFi.localIP().toString().c_str());
    LiveInput::onStaGotIp();
  }

  if (s_discPending) {
    s_discPending = false;
    if (s_connectStatus == ConnectStatus::Connecting) {
      const char *text = reasonText(s_discReason);
      if (text == nullptr) {
        char buf[24];
        snprintf(buf, sizeof(buf), "reason %u", s_discReason);
        failConnect(buf);
      } else {
        failConnect(text);
      }
    }
  }

  if (s_lostPending) {
    s_lostPending = false;
    LOG_C("wifi", "sta lost reason=%u", s_discReason);
    startAp();
    if (s_discReason == WIFI_REASON_NO_AP_FOUND) {
      failConnect("no network found");
    } else {
      s_connectStatus = ConnectStatus::Connecting;
      s_connectStart = millis();
      WiFi.setAutoReconnect(true);
    }
  }

  if (s_connectStatus == ConnectStatus::Connecting &&
      (millis() - s_connectStart) > kConnectTimeoutMs) {
    failConnect("timeout");
  }

  if (s_connectStatus == ConnectStatus::Connected &&
      WiFi.status() != WL_CONNECTED && !s_lostPending) {
    s_connectStatus = ConnectStatus::Connecting;
    s_connectStart = millis();
    startAp();
  }
}

} // namespace

void WifiSetup::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(kApSsid);
  WiFi.onEvent(onWifiEvent);

  startAp();

  s_server.on("/", HTTP_GET, sendPage);
  s_server.on("/scan", HTTP_GET, handleScan);
  s_server.on("/status", HTTP_GET, handleStatus);
  s_server.on("/connect", HTTP_POST, handleConnect);
  s_server.on("/forget", HTTP_POST, handleForget);
  s_server.on("/brightness", HTTP_POST, handleBrightness);
  s_server.on("/identify", HTTP_POST, handleIdentify);
  s_server.on("/live", HTTP_POST, handleLive);
  s_server.on("/play", HTTP_POST, handlePlay);
  s_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadFile);
  s_server.on("/name", HTTP_POST, handleName);
  s_server.on("/rename", HTTP_POST, handleRename);
  s_server.on("/file", HTTP_GET, handleFileGet);
  s_server.on("/order", HTTP_POST, handleOrder);
  s_server.on("/generate_204", HTTP_GET, handleCaptive);
  s_server.on("/gen_204", HTTP_GET, handleCaptive);
  s_server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
  s_server.on("/library/test/success.html", HTTP_GET, handleCaptive);
  s_server.on("/connecttest.txt", HTTP_GET, handleCaptive);
  s_server.on("/ncsi.txt", HTTP_GET, handleCaptive);
  s_server.on("/canonical.html", HTTP_GET, handleCaptive);
  s_server.on("/redirect", HTTP_GET, handleCaptive);
  s_server.on("/success.txt", HTTP_GET, handleCaptive);
  s_server.onNotFound(handleNotFound);
  s_server.begin();
  LOG_V("http", "listen :%u", kHttpPort);

  Preferences prefs;
  String ssid;
  String pass;
  if (prefs.begin(kPrefsNs, true)) {
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    prefs.end();
  }
  if (ssid.length()) {
    s_savedSsid = ssid;
    LOG_V("wifi", "saved ssid=%s", ssid.c_str());
    beginConnect(ssid, pass);
  } else {
    s_savedSsid = "";
    LOG_V("wifi", "no saved creds");
  }
}

void WifiSetup::service() {
  pollScan();
  pollConnect();
  if (LiveCfg::park() && LiveInput::active() &&
      WiFi.status() == WL_CONNECTED) {
    stopAp();
    return;
  }
  startAp();
  if (s_apUp) {
    s_dns.processNextRequest();
  }
  s_server.handleClient();
}
