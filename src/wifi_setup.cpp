#include "wifi_setup.h"

#include "board_profile.h"
#include "board_types.h"
#include "distribute.h"
#include "fixture.h"
#include "identify.h"
#include "led_bus.h"
#include "led_test.h"
#include "led_ctrl.h"
#include "live_cfg.h"
#include "live_input.h"
#include "log.h"
#include "node_id.h"
#include "ota.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "show_net.h"
#include "sync.h"
#include "sync_net.h"
#include "version.h"
#include "generated/wifi_setup_html_gz.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#if CONFIG_IDF_TARGET_ESP32C5
extern "C" void phy_bbpll_en_usb(bool en);
#endif
#include <cctype>
#include <cstdio>
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

enum class WifiBandPref : uint8_t {
  TwoG = 0,
  FiveG = 1,
  Auto = 2,
};

static constexpr uint16_t kHttpPort = 80;
static constexpr uint16_t kDnsPort = 53;
static constexpr uint32_t kConnectTimeoutMs = 20000;
static constexpr uint32_t kScanDwellMs = 75;
static constexpr uint32_t kDisconnectGraceMs = 800;
static constexpr uint32_t kRebootDelayMs = 300;
static constexpr int kMaxNets = 40;
// Arduino defaults the SoftAP to 4 stations; a phone, a PC and a few nodes
// need more.
static constexpr int kApMaxClients = 10;
static constexpr char kPrefsNs[] = "wifi";

static WebServer s_server(kHttpPort);
static DNSServer s_dns;
static String s_pendingSsid;
static String s_savedSsid;
static String s_savedPass;
static uint8_t s_savedBssid[6] = {};
static int32_t s_savedCh = 0;
static uint8_t s_savedGhz = 0;
static bool s_haveSavedBssid = false;
static uint8_t s_pendingBssid[6] = {};
static int32_t s_pendingCh = 0;
static bool s_havePendingBssid = false;
static bool s_pickBand = false;
static WifiBandPref s_bandPref = WifiBandPref::TwoG;
static char s_error[48];
static ConnectStatus s_connectStatus = ConnectStatus::Idle;
static bool s_scanRunning = false;
static bool s_haveScan = false;
static uint32_t s_connectStart = 0;
static bool s_connectTimerArmed = false;
static bool s_apUp = false;
static bool s_dnsUp = false;
// Member of a show network: which network the current attempt is for, and
// when to try again after a failure (show SSID, then the venue, alternating).
static bool s_memberOnShow = true;
static uint32_t s_memberRetryAt = 0;
static constexpr uint32_t kMemberRetryMs = 5000;
static uint32_t s_apFailMs = 0;
static volatile bool s_gotIp = false;
static volatile bool s_discPending = false;
static volatile bool s_lostPending = false;
static bool s_staIpPending = false;
static volatile uint8_t s_discReason = 0;
static uint32_t s_rebootAt = 0;

static File s_uploadFile;
static char s_uploadPath[kSdPathLen];
// Uploads land in a temp file (renamed over the target only on success) and
// reach the card in large blocks instead of ~1.4 KB HTTP chunks.
static constexpr char kUploadTmp[] = "/.whip-upload.tmp";
static constexpr size_t kUploadBufPsram = 32768;
static constexpr size_t kUploadBufMin = 4096;
static uint8_t *s_uploadBuf = nullptr;
static size_t s_uploadBufCap = 0;
static size_t s_uploadBufLen = 0;
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

// The portal is pre-gzipped at build time (scripts/gzip_portal.py). no-cache
// still revalidates every load, so a new firmware's page is never stale; an
// unchanged page costs a 304.
static void sendPage() {
  s_server.sendHeader("Cache-Control", "no-cache");
  s_server.sendHeader("ETag", kWifiSetupHtmlEtag);
  s_server.sendHeader("Connection", "close");
  if (s_server.header("If-None-Match") == kWifiSetupHtmlEtag) {
    s_server.send(304);
    return;
  }
  s_server.sendHeader("Content-Encoding", "gzip");
  s_server.send_P(200, "text/html",
                  reinterpret_cast<const char *>(kWifiSetupHtmlGz),
                  kWifiSetupHtmlGzLen);
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

static bool wifi5gCapable() {
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  return true;
#else
  return false;
#endif
}

static const char *bandPrefName(WifiBandPref pref) {
  switch (pref) {
  case WifiBandPref::FiveG:
    return "5g";
  case WifiBandPref::Auto:
    return "auto";
  case WifiBandPref::TwoG:
  default:
    return "2g";
  }
}

static bool parseBandPref(const String &s, WifiBandPref &out) {
  if (s == "2g") {
    out = WifiBandPref::TwoG;
    return true;
  }
  if (s == "5g") {
    out = WifiBandPref::FiveG;
    return true;
  }
  if (s == "auto") {
    out = WifiBandPref::Auto;
    return true;
  }
  return false;
}

static const char *linkName() {
  if (BoardProfile::radioKind() == BoardRadio::Eth) {
    return "wired";
  }
  if (WiFi.status() != WL_CONNECTED) {
    return nullptr;
  }
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  return WiFi.getBand() == WIFI_BAND_5G ? "5g" : "2g";
#else
  return "2g";
#endif
}

static void appendWifiBand(String &out) {
  out += "\"wifi_5g\":";
  out += wifi5gCapable() ? "true" : "false";
  if (wifi5gCapable()) {
    out += ",\"band\":";
    jsonEscape(out, String(bandPrefName(s_bandPref)));
  }
  const char *link = linkName();
  if (link != nullptr) {
    out += ",\"link\":";
    jsonEscape(out, String(link));
  }
}

static void saveBandPref() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("wifi", "nvs open failed");
    return;
  }
  prefs.putString("band", bandPrefName(s_bandPref));
  prefs.end();
}

static void loadBandPref() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return;
  }
  const String v = prefs.getString("band", "2g");
  prefs.end();
  WifiBandPref pref = WifiBandPref::TwoG;
  if (parseBandPref(v, pref)) {
    if (!wifi5gCapable() && pref != WifiBandPref::TwoG) {
      pref = WifiBandPref::TwoG;
    }
    s_bandPref = pref;
  }
}

static wifi_band_mode_t wantedBandMode() {
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  if (s_bandPref == WifiBandPref::Auto || s_bandPref == WifiBandPref::FiveG) {
    return WIFI_BAND_MODE_AUTO;
  }
#endif
  return WIFI_BAND_MODE_2G_ONLY;
}

static bool setRadioBand(wifi_band_mode_t want) {
#if CONFIG_IDF_TARGET_ESP32C5
  phy_bbpll_en_usb(true);
#endif
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  if (!WiFi.setBandMode(want)) {
    LOG_C("wifi", "band set failed want=%u pref=%s",
          static_cast<unsigned>(want), bandPrefName(s_bandPref));
    return false;
  }
#if CONFIG_IDF_TARGET_ESP32C5
  phy_bbpll_en_usb(true);
#endif
#else
  (void)want;
#endif
  return true;
}

static void applyBandPref() {
  const wifi_band_mode_t want = wantedBandMode();
  setRadioBand(want);
  const char *link = linkName();
  LOG_V("wifi", "band pref=%s link=%s", bandPrefName(s_bandPref),
        link != nullptr ? link : "-");
}

static bool parseHexByte(const char *p, uint8_t &out) {
  if (p[0] == '\0' || p[1] == '\0') {
    return false;
  }
  char buf[3] = {p[0], p[1], 0};
  char *end = nullptr;
  const long v = strtol(buf, &end, 16);
  if (end != buf + 2 || v < 0 || v > 255) {
    return false;
  }
  out = static_cast<uint8_t>(v);
  return true;
}

static bool parseBssid(const String &s, uint8_t out[6]) {
  if (s.length() != 17) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    if (i > 0 && s[static_cast<unsigned>(i) * 3 - 1] != ':') {
      return false;
    }
    if (!parseHexByte(s.c_str() + i * 3, out[i])) {
      return false;
    }
  }
  return true;
}

static void bssidToStr(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static void clearSavedBssidMem() {
  memset(s_savedBssid, 0, sizeof(s_savedBssid));
  s_savedCh = 0;
  s_savedGhz = 0;
  s_haveSavedBssid = false;
}

static void saveBssid(const uint8_t mac[6], int32_t ch, uint8_t ghz) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("wifi", "nvs open failed");
    return;
  }
  char buf[18];
  bssidToStr(mac, buf);
  prefs.putString("bssid", buf);
  prefs.putInt("ch", static_cast<int>(ch));
  prefs.putUChar("ghz", ghz);
  prefs.end();
  memcpy(s_savedBssid, mac, 6);
  s_savedCh = ch;
  s_savedGhz = ghz;
  s_haveSavedBssid = true;
  LOG_V("wifi", "saved bssid ch=%d ghz=%u", static_cast<int>(ch),
        static_cast<unsigned>(ghz));
}

static void clearSavedBssid() {
  Preferences prefs;
  if (prefs.begin(kPrefsNs, false)) {
    prefs.remove("bssid");
    prefs.remove("ch");
    prefs.remove("ghz");
    prefs.end();
  }
  clearSavedBssidMem();
}

static void beginConnect(const String &ssid, const String &pass, int32_t ch,
                         const uint8_t *bssid);

static bool setBandPref(const String &arg) {
  WifiBandPref pref = WifiBandPref::TwoG;
  if (!parseBandPref(arg, pref)) {
    return false;
  }
  if (!wifi5gCapable() && pref != WifiBandPref::TwoG) {
    return false;
  }
  s_bandPref = pref;
  saveBandPref();
  applyBandPref();
  return true;
}

static void saveCreds(const String &ssid, const String &pass, bool writePass) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    LOG_C("wifi", "nvs open failed");
    return;
  }
  prefs.putString("ssid", ssid);
  if (writePass) {
    prefs.putString("pass", pass);
  }
  prefs.end();
  s_savedSsid = ssid;
  if (writePass) {
    s_savedPass = pass;
  }
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
  prefs.remove("bssid");
  prefs.remove("ch");
  prefs.remove("ghz");
  prefs.end();
  s_savedSsid = "";
  s_savedPass = "";
  clearSavedBssidMem();
  LOG_V("wifi", "forgot saved network");
}

// Show Host: the show SSID on 10.77.0.1, DTIM 1 (group traffic is never held
// for dozing stations), no captive DNS.
static bool startShowHostAp() {
  const char *pass = ShowNet::pass();
  if (!WiFi.softAP(ShowNet::ssid(), pass[0] ? pass : nullptr,
                   ShowNet::channel(), 0, kApMaxClients)) {
    return false;
  }
  if (!WiFi.softAPConfig(kShowHostIp, kShowHostIp, kShowMask)) {
    LOG_C("ap", "show softAPConfig failed");
  }
  wifi_config_t cfg = {};
  if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
    cfg.ap.dtim_period = 1;
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
  }
  return true;
}

static void startAp() {
  if (s_apUp) {
    return;
  }
  if (s_apFailMs != 0 && (millis() - s_apFailMs) < 2000) {
    return;
  }
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  setRadioBand(WIFI_BAND_MODE_2G_ONLY);
#endif
  if (ShowNet::isHost()) {
    if (!startShowHostAp()) {
      LOG_C("ap", "show host softAP failed");
      s_apFailMs = millis();
      return;
    }
    s_apFailMs = 0;
    s_apUp = true;
    LOG_V("ap", "show host ssid=%s ch=%u ip=%s", ShowNet::ssid(),
          ShowNet::channel(), WiFi.softAPIP().toString().c_str());
    return;
  }
  if (!WiFi.softAP(kApSsid, kApPass, 1, 0, kApMaxClients)) {
    LOG_C("ap", "softAP failed");
    s_apFailMs = millis();
    return;
  }
  if (!WiFi.softAPConfig(kApIp, kApIp, kApMask)) {
    LOG_C("ap", "softAPConfig failed");
  }
  s_apFailMs = 0;
  s_dns.setTTL(0);
  if (!s_dns.start(kDnsPort, "*", kApIp)) {
    LOG_C("ap", "dns failed");
  } else {
    s_dnsUp = true;
  }
  s_apUp = true;
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
  if (s_bandPref != WifiBandPref::TwoG) {
    setRadioBand(WIFI_BAND_MODE_AUTO);
  }
#endif
  LOG_V("ap", "up ssid=%s ip=%s", kApSsid, WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!s_apUp) {
    return;
  }
  if (s_dnsUp) {
    s_dns.stop();
    s_dnsUp = false;
  }
  WiFi.softAPdisconnect(true);
  s_apUp = false;
  LOG_V("ap", "down (sta)");
}

static void failConnect(const char *why) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  s_connectStatus = ConnectStatus::Failed;
  setError(why);
  LOG_C("wifi", "connect failed ssid=%s %s", s_pendingSsid.c_str(), s_error);
  startAp();
  if (ShowNet::isMember()) {
    s_memberRetryAt = millis() + kMemberRetryMs;
  }
}

static void beginConnect(const String &ssid, const String &pass, int32_t ch,
                         const uint8_t *bssid) {
  s_pendingSsid = ssid;
  s_error[0] = '\0';
  s_gotIp = false;
  s_discPending = false;
  s_lostPending = false;
  s_pickBand = false;
  s_connectStart = millis();
  s_connectStatus = ConnectStatus::Connecting;
  WiFi.setAutoReconnect(true);
  const char *pw = pass.length() ? pass.c_str() : nullptr;
  if (bssid != nullptr) {
    memcpy(s_pendingBssid, bssid, 6);
    s_pendingCh = ch;
    s_havePendingBssid = true;
    WiFi.begin(ssid.c_str(), pw, ch, bssid);
    char mac[18];
    bssidToStr(bssid, mac);
    LOG_V("wifi", "connect ssid=%s ch=%d bssid=%s", ssid.c_str(),
          static_cast<int>(ch), mac);
  } else {
    s_havePendingBssid = false;
    s_pendingCh = 0;
    if (pw == nullptr) {
      WiFi.begin(ssid.c_str());
    } else {
      WiFi.begin(ssid.c_str(), pw);
    }
    LOG_V("wifi", "connect ssid=%s", ssid.c_str());
  }
}

static void startScan(WifiBandPref pref) {
  const wifi_band_mode_t mode =
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
      (pref == WifiBandPref::TwoG) ? WIFI_BAND_MODE_2G_ONLY
                                   : WIFI_BAND_MODE_AUTO;
#else
      WIFI_BAND_MODE_2G_ONLY;
#endif
  setRadioBand(mode);
  WiFi.scanDelete();
  s_haveScan = false;
  const int16_t rc = WiFi.scanNetworks(true, false, true, kScanDwellMs);
  if (rc == WIFI_SCAN_FAILED) {
    LOG_C("wifi", "scan start failed");
    s_scanRunning = false;
    return;
  }
  s_scanRunning = true;
  LOG_V("wifi", "scan start pref=%s", bandPrefName(pref));
}

static const char *modeName() {
  if (Playback::hold()) {
    return "play";
  }
  if (LiveInput::active()) {
    return "live";
  }
  if (Playback::playing()) {
    return "play";
  }
  return "idle";
}

static void appendSd(String &out) {
  out += "\"sd\":{\"ok\":";
  out += SdInfo::ok() ? "true" : "false";
  if (SdInfo::ok()) {
    out += ",\"type\":";
    jsonEscape(out, String(SdInfo::type()));
    out += ",\"size_mb\":";
    out += SdInfo::sizeMb();
    if (SdInfo::haveUsage()) {
      out += ",\"used_mb\":";
      out += SdInfo::usedMb();
      out += ",\"free_mb\":";
      out += SdInfo::freeMb();
    }
  }
  out += '}';
}

static void appendSeg(String &out, uint8_t i) {
  const PixelMapCfg &m = PixelMap::segment(i);
  out += "{\"proto\":";
  jsonEscape(out, String(PixelMap::protoName(m.proto)));
  out += ",\"order\":";
  jsonEscape(out, String(m.colorOrder));
  out += ",\"count\":";
  out += static_cast<unsigned>(m.pixelCount);
  out += ",\"white\":";
  out += m.white ? "true" : "false";
  out += ",\"cct\":";
  out += m.cct ? "true" : "false";
  out += ",\"artnet\":";
  out += static_cast<unsigned>(m.startArtNetUniverse);
  out += ",\"sacn\":";
  out += static_cast<unsigned>(m.startSacnUniverse);
  out += ",\"ch\":";
  out += static_cast<unsigned>(m.startChannel);
  out += ",\"ch_px\":";
  out += static_cast<unsigned>(m.channelsPerPixel);
  out += ",\"span\":";
  out += static_cast<unsigned>(PixelMap::universeSpan(i));
  out += ",\"fit\":";
  out += static_cast<unsigned>(PixelMap::firstUniversePixels(i));
  out += ",\"split\":";
  out += m.splitAcrossUniverses ? "true" : "false";
  out += ",\"bri\":";
  out += static_cast<unsigned>(m.brightness);
  out += '}';
}

static void appendMap(String &out) {
  const PixelMapCfg &m = PixelMap::cfg();
  out += "\"map\":{\"chip\":";
  jsonEscape(out, String(PixelMap::chipsetName()));
  out += ",\"order\":";
  jsonEscape(out, String(PixelMap::colorOrderName()));
  out += ",\"data\":";
  out += static_cast<unsigned>(m.dataGpio);
  out += ",\"clk\":";
  out += static_cast<unsigned>(m.clockGpio);
  out += ",\"count\":";
  out += static_cast<unsigned>(m.pixelCount);
  out += ",\"artnet\":";
  out += static_cast<unsigned>(m.startArtNetUniverse);
  out += ",\"sacn\":";
  out += static_cast<unsigned>(m.startSacnUniverse);
  out += ",\"ch\":";
  out += static_cast<unsigned>(m.startChannel);
  out += ",\"split\":";
  out += m.splitAcrossUniverses ? "true" : "false";
  out += ",\"white\":";
  out += m.white ? "true" : "false";
  out += ",\"cct\":";
  out += m.cct ? "true" : "false";
  out += ",\"ch_px\":";
  out += static_cast<unsigned>(m.channelsPerPixel);
  out += ",\"span\":";
  out += static_cast<unsigned>(PixelMap::universeSpan());
  out += ",\"fit\":";
  out += static_cast<unsigned>(PixelMap::firstUniversePixels());
  out += ",\"proto\":";
  jsonEscape(out, String(PixelMap::protoName(m.proto)));
  out += ",\"bri\":";
  out += static_cast<unsigned>(m.brightness);
  out += '}';
}

static void appendOutputs(String &out) {
  out += "\"outputs\":[";
  const uint8_t nOut = PixelMap::outputCount();
  const uint8_t nSeg = PixelMap::segmentCount();
  for (uint8_t o = 0; o < nOut; ++o) {
    if (o) {
      out += ',';
    }
    const uint8_t parent = PixelMap::firstSegmentOfOutput(o);
    const PixelMapCfg &m = PixelMap::segment(parent);
    out += "{\"data\":";
    out += static_cast<unsigned>(m.dataGpio);
    out += ",\"clk\":";
    out += static_cast<unsigned>(m.clockGpio);
    out += ",\"chip\":";
    jsonEscape(out, String(PixelMap::chipsetName(m.chipset)));
    out += ",\"count\":";
    out += static_cast<unsigned>(PixelMap::outputPixelCount(o));
    out += ",\"test\":\"";
    out += LedTest::modeName(o);
    out += "\",\"segs\":[";
    bool first = true;
    for (uint8_t i = 0; i < nSeg; ++i) {
      if (PixelMap::outputOfSegment(i) != o) {
        continue;
      }
      if (!first) {
        out += ',';
      }
      first = false;
      appendSeg(out, i);
    }
    out += "]}";
  }
  out += "],\"patch\":{\"max_out\":";
  out += static_cast<unsigned>(kPatchMaxOutputs);
  out += ",\"max_seg\":";
  out += static_cast<unsigned>(kPatchMaxSegments);
  out += ",\"max_px\":";
  out += static_cast<unsigned>(kLedCountMax);
  out += ",\"slots\":";
  out += static_cast<unsigned>(kLiveUniSlots);
  out += ",\"gpio_max\":";
  out += static_cast<unsigned>(BoardProfile::gpioMax());
  out += ",\"panel_w\":";
  out += static_cast<unsigned>(kMatrixWidth);
  out += ",\"panel_h\":";
  out += static_cast<unsigned>(kMatrixHeight);
  out += ",\"panel_px\":";
  out += static_cast<unsigned>(kLedCount);
  out += ",\"bri_warn\":";
  out += static_cast<unsigned>(kBrightnessWarn);
  out += ",\"led_data\":";
  out += static_cast<unsigned>(kLedPin);
  out += '}';
}

static void appendHex(String &out, uint32_t v) {
  char b[12];
  snprintf(b, sizeof(b), "\"%08x\"", static_cast<unsigned>(v));
  out += b;
}

// Shared network clock and the cue-bus roster.
static void appendClock(String &out) {
  out += "\"clock\":{\"id\":";
  appendHex(out, SyncNet::selfId());
  out += ",\"master\":";
  appendHex(out, SyncNet::clockMasterId());
  out += ",\"is_master\":";
  out += SyncNet::isClockMaster() ? "true" : "false";
  out += ",\"synced\":";
  out += SyncNet::synced() ? "true" : "false";
  out += ",\"rtt_us\":";
  out += static_cast<unsigned>(SyncNet::rttUs());
  out += ",\"peers\":[";
  for (uint8_t i = 0; i < SyncNet::peerCount(); ++i) {
    const SyncPeer *p = SyncNet::peerAt(i);
    if (i) {
      out += ',';
    }
    out += "{\"id\":";
    appendHex(out, p->id);
    out += ",\"name\":";
    jsonEscape(out, String(p->name));
    out += ",\"ip\":\"";
    out += p->ip.toString();
    out += "\",\"role\":";
    out += static_cast<unsigned>(p->role);
    out += ",\"api\":";
    out += static_cast<unsigned>(p->api);
    out += ",\"rtt_us\":";
    out += static_cast<unsigned>(p->rttUs);
    out += '}';
  }
  out += "]}";
}

static bool requestFromSoftAp() {
  // A Show Host's AP is the shared show network, not the private setup AP.
  if (!s_apUp || ShowNet::isHost()) {
    return false;
  }
  const IPAddress from = s_server.client().remoteIP();
  const IPAddress ap = WiFi.softAPIP();
  return from[0] == ap[0] && from[1] == ap[1] && from[2] == ap[2];
}

static void sendStatus(int code) {
  String out;
  out.reserve(12288);
  out += "{\"state\":\"";
  out += stateName();
  out += "\",\"ver\":";
  jsonEscape(out, String(kFirmwareVersion));
  out += ",\"api\":";
  out += static_cast<unsigned>(kFirmwareApi);
  out += ",\"chip\":";
  jsonEscape(out, String(BoardProfile::chip()));
  out += ",\"board\":";
  jsonEscape(out, String(BoardProfile::id()));
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
    // The saved password is for the portal on the node's own SoftAP only;
    // never hand it to anyone on the shared network.
    if (s_savedPass.length() && requestFromSoftAp()) {
      out += ",\"pass\":";
      jsonEscape(out, s_savedPass);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    out += ",\"ip\":\"";
    out += WiFi.localIP().toString();
    out += "\",\"rssi\":";
    out += WiFi.RSSI();
  }
  out += ",\"ap_ip\":\"";
  out += WiFi.softAPIP().toString();
  out += '"';
  if (s_connectStatus == ConnectStatus::Failed && s_error[0] != '\0') {
    out += ",\"error\":";
    jsonEscape(out, String(s_error));
  }
  out += ",\"shownet\":{\"role\":\"";
  out += ShowNet::roleName();
  out += "\",\"ssid\":";
  jsonEscape(out, String(ShowNet::ssid()));
  out += ",\"ch\":";
  out += static_cast<unsigned>(ShowNet::channel());
  out += "},";
  Distribute::appendStatus(out);
  out += ',';
  StreamTx::appendStatus(out);
  out += ',';
  Ota::appendStatus(out);
  out += ',';
  Fixture::appendStatus(out);
  out += ",\"bri\":";
  out += static_cast<unsigned>(LedCtrl::get());
  out += ',';
  appendSd(out);
  out += ',';
  appendMap(out);
  out += ',';
  appendOutputs(out);
  out += ",\"pins\":{\"led\":";
  out += static_cast<unsigned>(PixelMap::cfg().dataGpio);
  out += ",\"sd\":{\"cs\":";
  out += static_cast<unsigned>(BoardProfile::sdCs());
  out += ",\"mosi\":";
  out += static_cast<unsigned>(BoardProfile::sdMosi());
  out += ",\"clk\":";
  out += static_cast<unsigned>(BoardProfile::sdClk());
  out += ",\"miso\":";
  out += static_cast<unsigned>(BoardProfile::sdMiso());
  out += "},\"btn\":";
  if (BoardProfile::buttonPin() == kGpioUnset) {
    out += "null";
  } else {
    out += static_cast<unsigned>(BoardProfile::buttonPin());
  }
  out += '}';
  out += ",\"proto\":";
  jsonEscape(out, String(PixelMap::protoSummary()));
  out += ",\"fps\":";
  out += static_cast<unsigned>(LiveCfg::fps());
  out += ",\"buf\":";
  out += static_cast<unsigned>(LiveCfg::buf());
  out += ",\"park\":";
  jsonEscape(out, String(LiveCfg::parkName()));
  out += ",\"takeover\":";
  jsonEscape(out, String(LiveCfg::takeoverName()));
  out += ",\"unisync\":";
  jsonEscape(out, String(LiveCfg::uniSyncName()));
  out += ",\"loss\":";
  jsonEscape(out, String(LiveCfg::lossName()));
  out += ',';
  appendWifiBand(out);
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
  out += ",\"boot\":{\"src\":";
  jsonEscape(out, String(PlayCfg::bootSrcName()));
  out += ",\"path\":";
  jsonEscape(out, String(PlayCfg::bootPath()));
  out += ",\"file_loop\":";
  jsonEscape(out, String(PlayCfg::bootFileLoopName()));
  out += ",\"folder_rep\":";
  jsonEscape(out, String(PlayCfg::bootFolderRepName()));
  out += ",\"n\":";
  out += static_cast<unsigned>(PlayCfg::bootFolderN());
  out += "},\"now\":";
  jsonEscape(out, String(Playback::parked() ? "" : Playback::path()));
  out += ",\"paused\":";
  out += Playback::userPaused() ? "true" : "false";
  out += ",\"hold\":";
  out += Playback::hold() ? "true" : "false";
  out += ",\"files\":[";
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::fileAt(i)));
  }
  out += "],\"titles\":[";
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::titleAt(i)));
  }
  out += "],\"marks\":[";
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::markAt(i)));
  }
  out += "],\"groups\":[";
  for (uint8_t i = 0; i < SdInfo::fileCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::groupAt(i)));
  }
  out += "],\"dirs\":[";
  for (uint8_t i = 0; i < SdInfo::dirCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(SdInfo::dirAt(i)));
  }
  out += "],\"sync\":{\"group\":";
  jsonEscape(out, String(Sync::groupId()));
  out += ",\"members\":[";
  for (uint8_t i = 0; i < Sync::memberCount(); ++i) {
    if (i) {
      out += ',';
    }
    jsonEscape(out, String(Sync::memberNameAt(i)));
  }
  out += "],\"master\":";
  out += Sync::isLauncher() ? "true" : "false";
  out += ",\"follow\":";
  out += (Sync::inCue() && !Sync::isLauncher()) ? "true" : "false";
  out += ",\"paused\":";
  out += Sync::cuePaused() ? "true" : "false";
  out += ",\"waiting\":";
  out += Sync::waitingForCue() ? "true" : "false";
  out += ",\"pos\":";
  out += static_cast<unsigned>(Sync::inCue() ? Playback::showPosMs(millis()) : 0);
  out += "}},";
  appendClock(out);
  out += '}';
  sendJson(code, out);
}

static void sendScanResults() {
  String out;
  out.reserve(4096);
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
      const int32_t ch = WiFi.channel(i);
      out += ",\"ghz\":";
      out += ch > 14 ? "5" : "2";
      out += ",\"ch\":";
      out += static_cast<int>(ch);
      out += ",\"bssid\":";
      jsonEscape(out, WiFi.BSSIDstr(i));
      out += '}';
      ++emitted;
    }
  }
  out += "]}";
  sendJson(200, out);
}

static bool parseScanBandArg(WifiBandPref &out) {
  if (!s_server.hasArg("band")) {
    out = s_bandPref;
    return true;
  }
  if (!parseBandPref(s_server.arg("band"), out)) {
    return false;
  }
  if (!wifi5gCapable() && out != WifiBandPref::TwoG) {
    return false;
  }
  return true;
}

static void handleScan() {
  if (s_connectStatus == ConnectStatus::Connecting) {
    sendJson(200, "{\"state\":\"connecting\"}");
    return;
  }
  if (s_pickBand) {
    sendJson(200, "{\"state\":\"scanning\"}");
    return;
  }
  const bool force = s_server.hasArg("start");
  if (s_scanRunning) {
    sendJson(200, "{\"state\":\"scanning\"}");
    return;
  }
  if (force || !s_haveScan) {
    WifiBandPref scanPref = s_bandPref;
    if (!parseScanBandArg(scanPref)) {
      sendJson(400, "{\"error\":\"bad band\"}");
      return;
    }
    startScan(scanPref);
    sendJson(200, "{\"state\":\"scanning\"}");
    return;
  }
  sendScanResults();
}

static void sendStats() {
  const bool live = LiveInput::active();
  String out;
  out.reserve(4096);
  out += "{\"state\":\"";
  out += stateName();
  out += "\",\"ver\":";
  jsonEscape(out, String(kFirmwareVersion));
  out += ",\"api\":";
  out += static_cast<unsigned>(kFirmwareApi);
  out += ",\"chip\":";
  jsonEscape(out, String(BoardProfile::chip()));
  out += ",\"board\":";
  jsonEscape(out, String(BoardProfile::id()));
  out += ",\"name\":";
  jsonEscape(out, String(NodeId::longName()));
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
    out += "\",\"rssi\":";
    out += WiFi.RSSI();
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
  out += ',';
  appendSd(out);
  out += ",\"proto\":";
  jsonEscape(out, String(PixelMap::protoSummary()));
  out += ",\"fps\":";
  out += static_cast<unsigned>(LiveCfg::fps());
  out += ",\"buf\":";
  out += static_cast<unsigned>(LiveCfg::buf());
  out += ",\"park\":";
  jsonEscape(out, String(LiveCfg::parkName()));
  out += ",\"takeover\":";
  jsonEscape(out, String(LiveCfg::takeoverName()));
  out += ",\"unisync\":";
  jsonEscape(out, String(LiveCfg::uniSyncName()));
  out += ",\"loss\":";
  jsonEscape(out, String(LiveCfg::lossName()));
  out += ',';
  appendWifiBand(out);
  out += ",\"live\":";
  out += live ? "true" : "false";
  out += ",\"src\":";
  jsonEscape(out, String(live ? LiveInput::sourceName() : "none"));
  out += ",\"age_ms\":";
  out += static_cast<unsigned>(LiveInput::ageMs());
  out += ",\"queued\":";
  out += static_cast<unsigned>(LiveInput::queued());
  out += ",\"drops\":";
  out += static_cast<unsigned>(LiveInput::drops());
  out += ",\"pps\":";
  out += static_cast<unsigned>(LiveInput::pps());
  out += ",\"mode\":";
  jsonEscape(out, String(modeName()));
  out += ",\"heap\":";
  out += static_cast<unsigned>(ESP.getFreeHeap());
  out += ",\"psram\":";
  out += static_cast<unsigned>(ESP.getFreePsram());
  out += ",\"up_ms\":";
  out += static_cast<unsigned>(millis());
  out += ",\"play\":{\"now\":";
  jsonEscape(out, String(Playback::parked() ? "" : Playback::path()));
  out += ",\"parked\":";
  out += Playback::parked() ? "true" : "false";
  out += ",\"paused\":";
  out += Playback::userPaused() ? "true" : "false";
  out += ",\"hold\":";
  out += Playback::hold() ? "true" : "false";
  out += ",\"underrun\":";
  out += Playback::underrun() ? "true" : "false";
  out += ",\"frame\":";
  out += static_cast<unsigned>(Playback::frameIndex());
  out += "},";
  appendMap(out);
  out += ',';
  appendOutputs(out);
  out += '}';
  sendJson(200, out);
}

static void handleStatus() { sendStatus(200); }

static void handleStats() { sendStats(); }

static void handleIdentify() {
  if (LiveInput::active() && !Playback::hold()) {
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

static LedTestMode testModeFromArg(const String &mode) {
  if (mode == "off") {
    return LedTestMode::Off;
  }
  if (mode == "rainbow") {
    return LedTestMode::Rainbow;
  }
  if (mode == "cycle") {
    return LedTestMode::Cycle;
  }
  if (mode == "ends") {
    return LedTestMode::Ends;
  }
  return LedTestMode::Off;
}

static void handleTest() {
  if (LiveInput::active() && !Playback::hold()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!s_server.hasArg("i") || !s_server.hasArg("mode")) {
    sendJson(400, "{\"error\":\"bad test\"}");
    return;
  }
  const String iarg = s_server.arg("i");
  char *end = nullptr;
  const long idx = strtol(iarg.c_str(), &end, 10);
  if (end == iarg.c_str() || *end != '\0' || idx < 0 ||
      idx >= PixelMap::outputCount() || idx >= kPatchMaxOutputs) {
    sendJson(400, "{\"error\":\"bad i\"}");
    return;
  }
  const String modeArg = s_server.arg("mode");
  if (modeArg != "off" && modeArg != "rainbow" && modeArg != "cycle" &&
      modeArg != "ends") {
    sendJson(400, "{\"error\":\"bad mode\"}");
    return;
  }
  const LedTestMode mode = testModeFromArg(modeArg);
  if (mode != LedTestMode::Off) {
    Identify::cancel();
  }
  if (!LedTest::set(static_cast<uint8_t>(idx), mode)) {
    sendJson(400, "{\"error\":\"bad i\"}");
    return;
  }
  if (LedTest::active() && Playback::playing()) {
    Playback::pause();
  }
  String out = "{\"ok\":true,\"i\":";
  out += static_cast<unsigned>(idx);
  out += ",\"mode\":\"";
  out += LedTest::modeName(static_cast<uint8_t>(idx));
  out += "\"}";
  sendJson(200, out);
}

static void armReboot() {
  s_rebootAt = millis() + kRebootDelayMs;
  LOG_V("wifi", "reboot armed");
}

static void applyMapNow() {
  LedBus::requestApply();
  LiveInput::applyCfg();
  LedTest::cancel();
  LOG_V("map", "applied");
}

// Live-ok: a wedged show stream is when a remote restart is most useful.
static void handleReboot() {
  sendJson(200, "{\"ok\":true}");
  armReboot();
}

static void handleBrightness() {
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
  if (s_server.hasArg("i")) {
    const String iarg = s_server.arg("i");
    const long i = strtol(iarg.c_str(), &end, 10);
    if (end == iarg.c_str() || *end != '\0' || i < 0 ||
        i >= PixelMap::segmentCount()) {
      sendJson(400, "{\"error\":\"bad i\"}");
      return;
    }
    if (!PixelMap::setSegmentBrightness(static_cast<uint8_t>(i),
                                        static_cast<uint8_t>(v), true)) {
      sendJson(400, "{\"error\":\"bad i\"}");
      return;
    }
    sendStatus(200);
    return;
  }
  LedCtrl::set(static_cast<uint8_t>(v), true);
  sendStatus(200);
}

static bool parseGpioArg(const char *name, uint8_t &out) {
  if (!s_server.hasArg(name)) {
    return false;
  }
  const String arg = s_server.arg(name);
  char *end = nullptr;
  const long v = strtol(arg.c_str(), &end, 10);
  if (end == arg.c_str() || *end != '\0' || v < 0 ||
      v > static_cast<long>(BoardProfile::gpioMax())) {
    return false;
  }
  out = static_cast<uint8_t>(v);
  return true;
}

static void handlePins() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  uint8_t cs = 0;
  uint8_t mosi = 0;
  uint8_t clk = 0;
  uint8_t miso = 0;
  if (!parseGpioArg("cs", cs) || !parseGpioArg("mosi", mosi) ||
      !parseGpioArg("clk", clk) || !parseGpioArg("miso", miso)) {
    sendJson(400, "{\"error\":\"bad pins\"}");
    return;
  }
  if (!BoardProfile::setSdPins(cs, mosi, clk, miso, true)) {
    sendJson(400, "{\"error\":\"bad pins\"}");
    return;
  }
  Playback::park();
  delay(80);
  SdInfo::remount();
  sendStatus(200);
}

static bool parseBool01(const String &arg, bool &out) {
  if (arg == "1" || arg == "true" || arg == "yes") {
    out = true;
    return true;
  }
  if (arg == "0" || arg == "false" || arg == "no") {
    out = false;
    return true;
  }
  return false;
}

static String argName(const char *base, uint8_t i) {
  String s(base);
  s += static_cast<unsigned>(i);
  return s;
}

static bool parseLongArg(const String &arg, long minV, long maxV, long &out) {
  char *end = nullptr;
  const long v = strtol(arg.c_str(), &end, 10);
  if (end == arg.c_str() || *end != '\0' || v < minV || v > maxV) {
    return false;
  }
  out = v;
  return true;
}

static bool handleMapAll() {
  const String narg = s_server.arg("n");
  long n = 0;
  if (!parseLongArg(narg, 1, kPatchMaxSegments, n)) {
    return false;
  }
  PixelMapCfg segs[kPatchMaxSegments];
  for (uint8_t i = 0; i < static_cast<uint8_t>(n); ++i) {
    segs[i] = (i < PixelMap::segmentCount()) ? PixelMap::segment(i)
                                            : kMatrixPixelMap;
    const String protoK = argName("proto", i);
    const String chipK = argName("chip", i);
    const String dataK = argName("data", i);
    const String clkK = argName("clk", i);
    const String countK = argName("count", i);
    const String whiteK = argName("white", i);
    const String cctK = argName("cct", i);
    const String orderK = argName("order", i);
    const String uniK = argName("uni", i);
    const String chK = argName("ch", i);
    const String briK = argName("bri", i);
    if (s_server.hasArg(protoK) &&
        !PixelMap::parseProto(s_server.arg(protoK).c_str(), segs[i].proto)) {
      return false;
    }
    if (s_server.hasArg(chipK) &&
        !PixelMap::parseChipset(s_server.arg(chipK).c_str(), segs[i].chipset)) {
      return false;
    }
    if (s_server.hasArg(whiteK) &&
        !parseBool01(s_server.arg(whiteK), segs[i].white)) {
      return false;
    }
    if (s_server.hasArg(cctK) && !parseBool01(s_server.arg(cctK), segs[i].cct)) {
      return false;
    }
    if (s_server.hasArg(orderK)) {
      if (!PixelMap::parseOrder(s_server.arg(orderK).c_str(), segs[i].colorOrder,
                                segs[i].white, segs[i].cct)) {
        if (segs[i].white && segs[i].cct) {
          memcpy(segs[i].colorOrder, "grbwc", 6);
        } else if (segs[i].white) {
          memcpy(segs[i].colorOrder, "grbw", 5);
          segs[i].colorOrder[4] = '\0';
        } else if (segs[i].cct) {
          memcpy(segs[i].colorOrder, "grbc", 5);
          segs[i].colorOrder[4] = '\0';
        } else {
          memcpy(segs[i].colorOrder, "grb", 4);
          segs[i].colorOrder[3] = '\0';
        }
      }
    }
    long v = 0;
    if (s_server.hasArg(dataK)) {
      if (!parseLongArg(s_server.arg(dataK), 0, BoardProfile::gpioMax(), v)) {
        return false;
      }
      segs[i].dataGpio = static_cast<uint8_t>(v);
    }
    if (s_server.hasArg(clkK)) {
      if (!parseLongArg(s_server.arg(clkK), 0, BoardProfile::gpioMax(), v)) {
        return false;
      }
      segs[i].clockGpio = static_cast<uint8_t>(v);
    }
    if (s_server.hasArg(countK)) {
      if (!parseLongArg(s_server.arg(countK), 1, kLedCountMax, v)) {
        return false;
      }
      segs[i].pixelCount = static_cast<uint16_t>(v);
    }
    if (s_server.hasArg(chK)) {
      if (!parseLongArg(s_server.arg(chK), 1, kDmxUniverseSize, v)) {
        return false;
      }
      segs[i].startChannel = static_cast<uint16_t>(v);
    }
    if (s_server.hasArg(briK)) {
      if (!parseLongArg(s_server.arg(briK), 0, 255, v)) {
        return false;
      }
      segs[i].brightness = static_cast<uint8_t>(v);
    }
    if (s_server.hasArg(uniK)) {
      if (!parseLongArg(s_server.arg(uniK), 0, 32767, v)) {
        return false;
      }
      if (segs[i].proto == SegProto::Sacn) {
        const uint16_t sacn = v < 1 ? 1 : static_cast<uint16_t>(v);
        segs[i].startSacnUniverse = sacn;
        segs[i].startArtNetUniverse =
            static_cast<uint16_t>(sacn > 0 ? sacn - 1 : 0);
      } else {
        segs[i].startArtNetUniverse = static_cast<uint16_t>(v);
        segs[i].startSacnUniverse = static_cast<uint16_t>(v + 1);
      }
    }
    if (!PixelMap::needsClock(segs[i].chipset)) {
      segs[i].clockGpio = kClockGpioNone;
    }
  }
  return PixelMap::setAll(segs, static_cast<uint8_t>(n), true);
}

static void handleMap() {
  if (s_server.hasArg("n")) {
    if (!handleMapAll()) {
      sendJson(400, "{\"error\":\"bad map\"}");
      return;
    }
    sendStatus(200);
    applyMapNow();
    return;
  }
  const PixelMapCfg &cur = PixelMap::cfg();
  PixelMapSet in;
  in.proto = cur.proto;
  in.chipset = cur.chipset;
  memcpy(in.colorOrder, cur.colorOrder, sizeof(in.colorOrder));
  in.dataGpio = cur.dataGpio;
  in.clockGpio = cur.clockGpio;
  in.pixelCount = cur.pixelCount;
  in.startArtNetUniverse = cur.startArtNetUniverse;
  in.startChannel = cur.startChannel;
  in.white = cur.white;
  in.cct = cur.cct;
  in.brightness = cur.brightness;

  if (s_server.hasArg("proto") &&
      !PixelMap::parseProto(s_server.arg("proto").c_str(), in.proto)) {
    sendJson(400, "{\"error\":\"bad proto\"}");
    return;
  }
  if (s_server.hasArg("chip") &&
      !PixelMap::parseChipset(s_server.arg("chip").c_str(), in.chipset)) {
    sendJson(400, "{\"error\":\"bad chip\"}");
    return;
  }
  if (s_server.hasArg("white") &&
      !parseBool01(s_server.arg("white"), in.white)) {
    sendJson(400, "{\"error\":\"bad white\"}");
    return;
  }
  if (s_server.hasArg("cct") && !parseBool01(s_server.arg("cct"), in.cct)) {
    sendJson(400, "{\"error\":\"bad cct\"}");
    return;
  }
  if (s_server.hasArg("order") &&
      !PixelMap::parseOrder(s_server.arg("order").c_str(), in.colorOrder,
                            in.white, in.cct)) {
    sendJson(400, "{\"error\":\"bad order\"}");
    return;
  }
  if (s_server.hasArg("data")) {
    const String arg = s_server.arg("data");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 0 ||
        v > static_cast<long>(BoardProfile::gpioMax())) {
      sendJson(400, "{\"error\":\"bad data\"}");
      return;
    }
    in.dataGpio = static_cast<uint8_t>(v);
  }
  if (s_server.hasArg("clk")) {
    const String arg = s_server.arg("clk");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 0 ||
        v > static_cast<long>(BoardProfile::gpioMax())) {
      sendJson(400, "{\"error\":\"bad clk\"}");
      return;
    }
    in.clockGpio = static_cast<uint8_t>(v);
  }
  if (s_server.hasArg("count")) {
    const String arg = s_server.arg("count");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 1 || v > kLedCountMax) {
      sendJson(400, "{\"error\":\"bad count\"}");
      return;
    }
    in.pixelCount = static_cast<uint16_t>(v);
  }
  if (s_server.hasArg("uni")) {
    const String arg = s_server.arg("uni");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 0 || v > 32767) {
      sendJson(400, "{\"error\":\"bad uni\"}");
      return;
    }
    in.startArtNetUniverse = static_cast<uint16_t>(v);
  }
  if (s_server.hasArg("ch")) {
    const String arg = s_server.arg("ch");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 1 || v > kDmxUniverseSize) {
      sendJson(400, "{\"error\":\"bad ch\"}");
      return;
    }
    in.startChannel = static_cast<uint16_t>(v);
  }
  if (s_server.hasArg("bri")) {
    const String arg = s_server.arg("bri");
    char *end = nullptr;
    const long v = strtol(arg.c_str(), &end, 10);
    if (end == arg.c_str() || *end != '\0' || v < 0 || v > 255) {
      sendJson(400, "{\"error\":\"bad bri\"}");
      return;
    }
    in.brightness = static_cast<uint8_t>(v);
  }
  if (s_server.hasArg("uni") && in.proto == SegProto::Sacn) {
    const uint16_t sacn =
        in.startArtNetUniverse < 1 ? 1 : in.startArtNetUniverse;
    in.startArtNetUniverse = static_cast<uint16_t>(sacn - 1);
  }
  if (!PixelMap::validOrder(in.colorOrder, in.white, in.cct)) {
    if (in.white && in.cct) {
      memcpy(in.colorOrder, "grbwc", 6);
    } else if (in.white) {
      memcpy(in.colorOrder, "grbw", 5);
      in.colorOrder[4] = '\0';
      in.colorOrder[5] = '\0';
    } else if (in.cct) {
      memcpy(in.colorOrder, "grbc", 5);
      in.colorOrder[4] = '\0';
      in.colorOrder[5] = '\0';
    } else {
      memcpy(in.colorOrder, "grb", 4);
      in.colorOrder[3] = '\0';
      in.colorOrder[4] = '\0';
      in.colorOrder[5] = '\0';
    }
  }
  if (!PixelMap::set(in, true)) {
    sendJson(400, "{\"error\":\"bad map\"}");
    return;
  }
  sendStatus(200);
  applyMapNow();
}

static void handleLive() {
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

  bool takeover = LiveCfg::takeover();
  if (s_server.hasArg("takeover")) {
    const String takeArg = s_server.arg("takeover");
    if (takeArg == "yes") {
      takeover = true;
    } else if (takeArg == "no") {
      takeover = false;
    } else {
      sendJson(400, "{\"error\":\"bad takeover\"}");
      return;
    }
  }

  bool uniSync = LiveCfg::uniSync();
  if (s_server.hasArg("unisync")) {
    const String uniArg = s_server.arg("unisync");
    if (uniArg == "yes") {
      uniSync = true;
    } else if (uniArg == "no") {
      uniSync = false;
    } else {
      sendJson(400, "{\"error\":\"bad unisync\"}");
      return;
    }
  }

  LiveLoss loss = LiveCfg::loss();
  if (s_server.hasArg("loss")) {
    const String lossArg = s_server.arg("loss");
    if (lossArg == "play") {
      loss = LiveLoss::Play;
    } else if (lossArg == "hold") {
      loss = LiveLoss::Hold;
    } else if (lossArg == "black") {
      loss = LiveLoss::Black;
    } else {
      sendJson(400, "{\"error\":\"bad loss\"}");
      return;
    }
  }

  if (!LiveCfg::set(proto, fps, buf, park, takeover, uniSync, loss, true)) {
    sendJson(400, "{\"error\":\"bad live\"}");
    return;
  }
  sendStatus(200);
}

static bool endsWithDmx(const char *p) {
  const size_t n = p ? strlen(p) : 0;
  return n >= 4 && strcasecmp(p + n - 4, ".dmx") == 0;
}

static bool sidecarPath(const char *dmx, char *out, size_t n) {
  if (!dmx || !out || n < 8) {
    return false;
  }
  const size_t len = strlen(dmx);
  if (len < 5 || len + 2 > n || !endsWithDmx(dmx)) {
    return false;
  }
  snprintf(out, n, "%s", dmx);
  const size_t base = len - 4;
  if (base + 6 > n) {
    return false;
  }
  memcpy(out + base, ".json", 6);
  return true;
}

static void removeSidecar(const char *dmx) {
  char side[kSdPathLen];
  if (sidecarPath(dmx, side, sizeof(side)) && SD.exists(side)) {
    SD.remove(side);
  }
}

static void renameSidecar(const char *from, const char *to) {
  char src[kSdPathLen];
  char dest[kSdPathLen];
  if (!sidecarPath(from, src, sizeof(src)) ||
      !sidecarPath(to, dest, sizeof(dest))) {
    return;
  }
  if (strcmp(src, dest) == 0) {
    return;
  }
  if (!SD.exists(src)) {
    return;
  }
  if (SD.exists(dest)) {
    SD.remove(dest);
  }
  SD.rename(src, dest);
}

static void sanitizeTitle(const char *in, char *out, size_t n) {
  if (!out || n < 2) {
    return;
  }
  out[0] = '\0';
  if (!in) {
    return;
  }
  size_t i = 0;
  while (*in && static_cast<unsigned char>(*in) <= ' ') {
    in++;
  }
  while (*in && i + 1 < n && i + 1 < kSdTitleLen) {
    const unsigned char c = static_cast<unsigned char>(*in++);
    if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
        c == '\\' || c == '|' || c == '?' || c == '*') {
      continue;
    }
    out[i++] = static_cast<char>(c);
  }
  while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '.')) {
    i--;
  }
  out[i] = '\0';
}

static void jsonWriteEscaped(File &f, const char *s) {
  if (!s) {
    return;
  }
  for (const char *p = s; *p; ++p) {
    if (*p == '\\' || *p == '"') {
      f.write('\\');
    }
    f.write(static_cast<uint8_t>(*p));
  }
}

static bool writeSidecarName(const char *dmx, const char *name,
                             const char *group, const char *members,
                             const char *kind, uint32_t dur) {
  char side[kSdPathLen];
  if (!sidecarPath(dmx, side, sizeof(side))) {
    return false;
  }
  char keepGroup[40] = {};
  char keepMembers[512] = {};
  char keepKind[8] = {};
  uint32_t keepDur = 0;
  if ((!group || !group[0]) && SD.exists(side)) {
    File in = SD.open(side, FILE_READ);
    if (in) {
      char buf[768];
      const int n = in.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf) - 1);
      in.close();
      if (n > 0) {
        buf[n] = '\0';
        const char *g = strstr(buf, "\"group\"");
        if (g) {
          const char *q = strchr(g + 7, '"');
          if (q) {
            q = strchr(q + 1, '"');
            if (q) {
              q++;
              size_t i = 0;
              while (*q && *q != '"' && i + 1 < sizeof(keepGroup)) {
                keepGroup[i++] = *q++;
              }
              keepGroup[i] = '\0';
            }
          }
        }
        const char *m = strstr(buf, "\"members\"");
        if (m) {
          const char *b = strchr(m, '[');
          const char *e = b ? strchr(b, ']') : nullptr;
          if (b && e && static_cast<size_t>(e - b + 1) < sizeof(keepMembers)) {
            memcpy(keepMembers, b, static_cast<size_t>(e - b + 1));
            keepMembers[e - b + 1] = '\0';
          }
        }
        const char *d = strstr(buf, "\"dur\"");
        if (d && dur == 0) {
          const char *c = strchr(d + 5, ':');
          keepDur = c ? static_cast<uint32_t>(strtoul(c + 1, nullptr, 10)) : 0;
        }
        const char *k = strstr(buf, "\"kind\"");
        if (k) {
          const char *q = strchr(k + 6, '"');
          if (q) {
            q = strchr(q + 1, '"');
            if (q) {
              q++;
              size_t i = 0;
              while (*q && *q != '"' && i + 1 < sizeof(keepKind)) {
                keepKind[i++] = *q++;
              }
              keepKind[i] = '\0';
            }
          }
        }
      }
    }
  }
  const char *useGroup = (group && group[0]) ? group : keepGroup;
  const char *useMembers = (members && members[0]) ? members : keepMembers;
  const char *useKind = (kind && kind[0]) ? kind : keepKind;
  if (useKind[0] && strcmp(useKind, "uni") != 0 && strcmp(useKind, "split") != 0) {
    useKind = "";
  }
  if ((!name || !name[0]) && !useGroup[0]) {
    if (SD.exists(side)) {
      SD.remove(side);
    }
    return true;
  }
  if (SD.exists(side)) {
    SD.remove(side);
  }
  File f = SD.open(side, FILE_WRITE);
  if (!f) {
    return false;
  }
  f.print("{\"name\":\"");
  jsonWriteEscaped(f, name ? name : "");
  f.print("\"");
  if (useGroup[0]) {
    f.print(",\"sync\":{\"group\":\"");
    jsonWriteEscaped(f, useGroup);
    f.print("\",\"members\":");
    f.print(useMembers[0] ? useMembers : "[]");
    if (useKind[0]) {
      f.print(",\"kind\":\"");
      jsonWriteEscaped(f, useKind);
      f.print("\"");
    }
    const uint32_t useDur = dur ? dur : keepDur;
    if (useDur) {
      f.print(",\"dur\":");
      f.print(useDur);
    }
    f.print("}");
  }
  f.print("}\n");
  f.close();
  return true;
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
  if (PlayCfg::bootSrc() == PlaySrc::File &&
      strcmp(PlayCfg::bootPath(), from) == 0) {
    PlayCfg::setStartup(PlaySrc::File, to, PlayCfg::bootFileLoop(),
                        PlayCfg::bootFolderRep(), PlayCfg::bootFolderN());
  }
  if (PlayCfg::src() != PlaySrc::File || strcmp(PlayCfg::path(), from) != 0) {
    return;
  }
  PlayCfg::set(PlaySrc::File, to, PlayCfg::fileLoop(), PlayCfg::folderRep(),
               PlayCfg::folderN(), false);
}

static void resetUploadState() {
  s_uploadOk = false;
  s_uploadBytes = 0;
  s_uploadError = nullptr;
  s_uploadPath[0] = '\0';
}

static bool flushUploadBuf() {
  if (s_uploadBufLen == 0) {
    return true;
  }
  const size_t n = s_uploadBufLen;
  s_uploadBufLen = 0;
  return s_uploadFile && s_uploadFile.write(s_uploadBuf, n) == n;
}

static void freeUploadBuf() {
  free(s_uploadBuf);
  s_uploadBuf = nullptr;
  s_uploadBufCap = 0;
  s_uploadBufLen = 0;
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

static void ensureUploadParents(const char *path) {
  if (!path || path[0] != '/') {
    return;
  }
  char dir[kSdPathLen];
  dir[0] = '\0';
  const char *p = path + 1;
  while (*p) {
    const char *slash = strchr(p, '/');
    if (!slash) {
      break;
    }
    const size_t used = dir[0] ? strlen(dir) : 0;
    const size_t seg = static_cast<size_t>(slash - p);
    if (!seg || used + 1 + seg >= kSdPathLen) {
      return;
    }
    snprintf(dir + used, kSdPathLen - used, "/%.*s", static_cast<int>(seg), p);
    SD.mkdir(dir);
    p = slash + 1;
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
    if (!SdInfo::lock(2000)) {
      s_uploadError = "busy";
      return;
    }
    s_uploadLocked = true;
    freeUploadBuf();
    s_uploadBufCap = psramFound() ? kUploadBufPsram : kUploadBufMin;
    s_uploadBuf = static_cast<uint8_t *>(psramFound() ? ps_malloc(s_uploadBufCap)
                                                      : malloc(s_uploadBufCap));
    if (s_uploadBuf == nullptr) {
      s_uploadBufCap = 0;
    }
    ensureUploadParents(s_uploadPath);
    SD.remove(kUploadTmp);
    s_uploadFile = SD.open(kUploadTmp, FILE_WRITE);
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
    bool ok = true;
    if (s_uploadBufCap == 0) {
      ok = s_uploadFile.write(up.buf, up.currentSize) == up.currentSize;
    } else {
      size_t at = 0;
      while (ok && at < up.currentSize) {
        size_t take = s_uploadBufCap - s_uploadBufLen;
        if (take > up.currentSize - at) {
          take = up.currentSize - at;
        }
        memcpy(s_uploadBuf + s_uploadBufLen, up.buf + at, take);
        s_uploadBufLen += take;
        at += take;
        if (s_uploadBufLen == s_uploadBufCap) {
          ok = flushUploadBuf();
        }
      }
    }
    if (!ok) {
      s_uploadError = "write failed";
    } else {
      s_uploadBytes += up.currentSize;
    }
    yield();
    return;
  }
  if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    const bool aborted = up.status == UPLOAD_FILE_ABORTED;
    if (!aborted && !s_uploadError && !flushUploadBuf()) {
      s_uploadError = "write failed";
    }
    closeUploadFile();
    freeUploadBuf();
    if (aborted && !s_uploadError) {
      s_uploadError = "aborted";
    }
    // The previous file at the target path stays intact unless the new one
    // arrived whole.
    if (s_uploadLocked) {
      if (s_uploadPath[0] && !s_uploadError) {
        if (SD.exists(s_uploadPath)) {
          SD.remove(s_uploadPath);
        }
        if (!SD.rename(kUploadTmp, s_uploadPath)) {
          s_uploadError = "write failed";
        }
      }
      SD.remove(kUploadTmp);
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

// GET /fixture: the advanced patch (see fixture.h).
static void handleFixtureGet() {
  String out;
  out.reserve(2048);
  Fixture::appendJson(out);
  sendJson(200, out);
}

// POST /fixture: en, mode (basic|dim|rgb|full), proto (artnet|sacn), uni, ch, n,
// then s<i>n (name) and s<i>px ("0-11,24-35") for i < n.
static void handleFixturePost() {
  FixMode mode = FixMode::Dim;
  if (!Fixture::parseMode(s_server.arg("mode").c_str(), mode)) {
    sendJson(400, "{\"error\":\"bad mode\"}");
    return;
  }
  const String proto = s_server.arg("proto");
  if (proto != "artnet" && proto != "sacn") {
    sendJson(400, "{\"error\":\"bad proto\"}");
    return;
  }
  const long uni = s_server.arg("uni").toInt();
  const long ch = s_server.arg("ch").toInt();
  const long n = s_server.arg("n").toInt();
  if (uni < 0 || uni > 63999 || ch < 1 || ch > 512 || n < 0 || n > kFixMaxSubs) {
    sendJson(400, "{\"error\":\"bad patch\"}");
    return;
  }
  Fixture::stageBegin(s_server.arg("en") == "1", mode, proto == "sacn",
                      static_cast<uint16_t>(uni), static_cast<uint16_t>(ch));
  const char *err = nullptr;
  bool ok = true;
  for (long i = 0; ok && i < n; ++i) {
    const String key = String("s") + i;
    ok = Fixture::stageSub(s_server.arg(key + "n").c_str(), s_server.arg(key + "px").c_str(), err);
  }
  if (ok) {
    ok = Fixture::stageCommit(err);
  }
  if (!ok) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(400, out);
    return;
  }
  handleFixtureGet();
}

// GET /fixture/names?from=&n= (n <= 128).
static void handleFixtureNamesGet() {
  long from = s_server.arg("from").toInt();
  long n = s_server.hasArg("n") ? s_server.arg("n").toInt() : 128;
  from = from < 0 ? 0 : (from > kLedCountMax ? kLedCountMax : from);
  n = n < 0 ? 0 : (n > 128 ? 128 : n);
  String out;
  out.reserve(static_cast<size_t>(n) * 28 + 64);
  Fixture::appendNames(out, static_cast<uint16_t>(from), static_cast<uint16_t>(n));
  sendJson(200, out);
}

// POST /fixture/locate: px ("0-11,40"), ms (0 cancels, max 15000).
static void handleFixtureLocate() {
  if (LiveInput::active() && !Playback::hold()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  const long ms = s_server.hasArg("ms") ? s_server.arg("ms").toInt() : 10000;
  const char *err = nullptr;
  if (!Fixture::locate(s_server.arg("px").c_str(), ms < 0 ? 0 : static_cast<uint32_t>(ms), err)) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(400, out);
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

// POST /fixture/names: from, names (one per line, 23 chars kept).
static void handleFixtureNamesPost() {
  const long from = s_server.arg("from").toInt();
  if (from < 0 || from >= kLedCountMax || !s_server.hasArg("names")) {
    sendJson(400, "{\"error\":\"bad names\"}");
    return;
  }
  const char *err = nullptr;
  if (!Fixture::setNames(static_cast<uint16_t>(from), s_server.arg("names"), err)) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(500, out);
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

// POST /ota?force=0|1, multipart part "firmware": see ota.h.
static void handleOtaFile() {
  HTTPUpload &up = s_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Ota::uploadStart(s_server.clientContentLength(), s_server.arg("force") == "1");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    Ota::uploadWrite(up.buf, up.currentSize);
    yield();
  } else if (up.status == UPLOAD_FILE_END) {
    Ota::uploadEnd(false);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Ota::uploadEnd(true);
  }
}

static void handleOtaDone() {
  const char *err = Ota::uploadError();
  if (!err && Ota::uploadOk()) {
    String out = "{\"ok\":true,\"ver\":";
    jsonEscape(out, String(Ota::uploadVersion()));
    out += ",\"reboot\":true}";
    sendJson(200, out);
    armReboot();
    return;
  }
  if (!err) {
    err = "no file";
  }
  int code = 500;
  if (strcmp(err, "busy") == 0 || strcmp(err, "too large") == 0 ||
      strcmp(err, "no ota slot") == 0) {
    code = 409;
  } else if (strcmp(err, "other board") == 0 || strcmp(err, "not a whip image") == 0 ||
             strcmp(err, "not firmware") == 0 || strcmp(err, "no file") == 0 ||
             strcmp(err, "aborted") == 0) {
    code = 400;
  }
  String out = "{\"error\":";
  jsonEscape(out, String(err));
  out += '}';
  sendJson(code, out);
}

// POST /ota/peers?force=0|1: send this image to older same-board nodes.
static void handleOtaPeers() {
  const char *err = nullptr;
  if (!Ota::startPeers(s_server.arg("force") == "1", err)) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(409, out);
    return;
  }
  sendStatus(200);
}

static void handleName() {
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
  if (ok) {
    renameSidecar(from.c_str(), to.c_str());
  }
  SdInfo::unlock();
  if (!ok) {
    sendJson(500, "{\"error\":\"rename failed\"}");
    return;
  }
  SdInfo::refreshTree();
  remapPlayPath(from.c_str(), to.c_str());
  sendStatus(200);
}

static void handleMeta() {
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
  char title[kSdTitleLen];
  sanitizeTitle(s_server.arg("name").c_str(), title, sizeof(title));
  const String groupArg = s_server.arg("sync_group");
  const String membersArg = s_server.arg("sync_members");
  const String kindArg = s_server.arg("sync_kind");
  const uint32_t durArg =
      static_cast<uint32_t>(strtoul(s_server.arg("sync_dur").c_str(), nullptr, 10));
  if (!SdInfo::lock(2000)) {
    sendJson(503, "{\"error\":\"busy\"}");
    return;
  }
  if (!SD.exists(path.c_str())) {
    SdInfo::unlock();
    sendJson(404, "{\"error\":\"missing\"}");
    return;
  }
  const bool ok = writeSidecarName(path.c_str(), title, groupArg.c_str(),
                                   membersArg.c_str(), kindArg.c_str(), durArg);
  SdInfo::unlock();
  if (!ok) {
    sendJson(500, "{\"error\":\"meta failed\"}");
    return;
  }
  SdInfo::refreshTree();
  if (Playback::path()[0] && strcmp(Playback::path(), path.c_str()) == 0) {
    Sync::onPlayFile(path.c_str());
  }
  LOG_V("http", "meta %s", path.c_str());
  sendStatus(200);
}

static void handleDelete() {
  if (LiveInput::active()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (!SdInfo::ok()) {
    sendJson(503, "{\"error\":\"no sd\"}");
    return;
  }

  char paths[kSdMaxListFiles][kSdPathLen];
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
    bool dup = false;
    for (uint8_t j = 0; j < n; ++j) {
      if (strcmp(paths[j], p.c_str()) == 0) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    snprintf(paths[n], kSdPathLen, "%s", p.c_str());
    n = static_cast<uint8_t>(n + 1);
  }
  if (n == 0) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }

  Playback::park();
  delay(80);
  if (!SdInfo::lock(2000)) {
    sendJson(503, "{\"error\":\"busy\"}");
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    if (!SD.exists(paths[i])) {
      SdInfo::unlock();
      sendJson(404, "{\"error\":\"missing\"}");
      return;
    }
  }
  bool hitPlaylist = false;
  bool hitStartup = false;
  for (uint8_t i = 0; i < n; ++i) {
    if (PlayCfg::src() == PlaySrc::File &&
        strcmp(PlayCfg::path(), paths[i]) == 0) {
      hitPlaylist = true;
    }
    if (PlayCfg::bootSrc() == PlaySrc::File &&
        strcmp(PlayCfg::bootPath(), paths[i]) == 0) {
      hitStartup = true;
    }
    if (!SD.remove(paths[i])) {
      SdInfo::unlock();
      sendJson(500, "{\"error\":\"delete failed\"}");
      return;
    }
    removeSidecar(paths[i]);
  }
  SdInfo::unlock();
  SdInfo::refreshTree();
  if (hitStartup) {
    PlayCfg::setStartup(PlaySrc::Root, "/", PlayCfg::bootFileLoop(),
                        PlayCfg::bootFolderRep(), PlayCfg::bootFolderN());
  }
  if (hitPlaylist) {
    PlayCfg::set(PlaySrc::Root, "/", PlayCfg::fileLoop(), PlayCfg::folderRep(),
                 PlayCfg::folderN(), false);
    Playback::park();
  }
  LOG_V("http", "delete n=%u", static_cast<unsigned>(n));
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
  if (ok) {
    for (uint8_t i = 0; i < n; ++i) {
      renameSidecar(froms[i], dests[i]);
    }
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

static bool argOverride() {
  const String o = s_server.arg("override");
  return o == "1" || o == "yes" || o == "true";
}

static bool readPlayForm(PlaySrc &src, String &path, PlayFileLoop &fileLoop,
                         PlayFolderRep &folderRep, uint8_t &n) {
  const String srcArg = s_server.arg("src");
  src = PlayCfg::src();
  if (srcArg == "root") {
    src = PlaySrc::Root;
  } else if (srcArg == "file") {
    src = PlaySrc::File;
  } else if (srcArg == "folder") {
    src = PlaySrc::Folder;
  } else if (srcArg.length()) {
    sendJson(400, "{\"error\":\"bad src\"}");
    return false;
  }

  path = s_server.hasArg("path") ? s_server.arg("path") : String(PlayCfg::path());

  fileLoop = PlayCfg::fileLoop();
  const String flpArg = s_server.arg("file_loop");
  if (flpArg == "one") {
    fileLoop = PlayFileLoop::One;
  } else if (flpArg == "all") {
    fileLoop = PlayFileLoop::All;
  } else if (flpArg.length()) {
    sendJson(400, "{\"error\":\"bad file_loop\"}");
    return false;
  }

  folderRep = PlayCfg::folderRep();
  const String frpArg = s_server.arg("folder_rep");
  if (frpArg == "forever") {
    folderRep = PlayFolderRep::Forever;
  } else if (frpArg == "count") {
    folderRep = PlayFolderRep::Count;
  } else if (frpArg.length()) {
    sendJson(400, "{\"error\":\"bad folder_rep\"}");
    return false;
  }

  n = PlayCfg::folderN();
  if (s_server.hasArg("n")) {
    const String nArg = s_server.arg("n");
    char *end = nullptr;
    const long v = strtol(nArg.c_str(), &end, 10);
    if (end == nArg.c_str() || *end != '\0' || v < 1 || v > 99) {
      sendJson(400, "{\"error\":\"bad n\"}");
      return false;
    }
    n = static_cast<uint8_t>(v);
  }
  return true;
}

static void handlePlay() {
  const String srcArg = s_server.arg("src");
  const String actionArg = s_server.arg("action");
  if (actionArg == "startup") {
    if (srcArg == "none") {
      if (!PlayCfg::setStartup(PlaySrc::None, "/", PlayFileLoop::All,
                               PlayFolderRep::Forever, 1)) {
        sendJson(400, "{\"error\":\"bad play\"}");
        return;
      }
      sendStatus(200);
      return;
    }
    PlaySrc src = PlaySrc::Root;
    String path;
    PlayFileLoop fileLoop = PlayFileLoop::All;
    PlayFolderRep folderRep = PlayFolderRep::Forever;
    uint8_t n = 1;
    if (!readPlayForm(src, path, fileLoop, folderRep, n)) {
      return;
    }
    if (!PlayCfg::setStartup(src, path.c_str(), fileLoop, folderRep, n)) {
      sendJson(400, "{\"error\":\"bad play\"}");
      return;
    }
    sendStatus(200);
    return;
  }
  const bool release = srcArg == "stop" || actionArg == "stop" ||
                       srcArg == "live" || actionArg == "live";
  if (LiveInput::active() && !Playback::hold() && !argOverride() && !release) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  if (srcArg == "live" || actionArg == "live") {
    Playback::setHold(false);
    if (LiveInput::active()) {
      Sync::releaseToLive();
    }
    sendStatus(200);
    return;
  }
  if (srcArg == "stop" || actionArg == "stop") {
    Playback::park();
    Sync::noteStopped();
    sendStatus(200);
    return;
  }
  // Inside a group cue, pause / resume act on every member at one network
  // time; otherwise they hold / continue this node only.
  if (srcArg == "pause" || actionArg == "pause") {
    if (!Sync::noteLocalPause()) {
      Playback::userPause();
    }
    sendStatus(200);
    return;
  }
  if (srcArg == "resume" || actionArg == "resume") {
    if (Sync::noteLocalResume()) {
      sendStatus(200);
      return;
    }
    if (argOverride()) {
      Playback::setHold(true);
    }
    Playback::userResume();
    sendStatus(200);
    return;
  }

  PlaySrc src = PlaySrc::Root;
  String pathArg;
  PlayFileLoop fileLoop = PlayFileLoop::All;
  PlayFolderRep folderRep = PlayFolderRep::Forever;
  uint8_t n = 1;
  if (!readPlayForm(src, pathArg, fileLoop, folderRep, n)) {
    return;
  }

  if (argOverride()) {
    Playback::setHold(true);
  }
  if (!PlayCfg::set(src, pathArg.c_str(), fileLoop, folderRep, n, false, false)) {
    if (argOverride()) {
      Playback::setHold(false);
    }
    sendJson(400, "{\"error\":\"bad play\"}");
    return;
  }
  Playback::reload();
  Sync::noteLocalTrigger();
  sendStatus(200);
}

static int32_t parseChannelArg(const String &s) {
  if (!s.length()) {
    return 0;
  }
  char *end = nullptr;
  const long v = strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0' || v < 0 || v > 196) {
    return -1;
  }
  return static_cast<int32_t>(v);
}

static bool postedBssidOk(WifiBandPref pref, bool haveBssid, int32_t ch) {
  if (!haveBssid) {
    return false;
  }
  if (pref == WifiBandPref::FiveG) {
    return ch > 14;
  }
  if (pref == WifiBandPref::TwoG) {
    return ch <= 14;
  }
  return ch > 14;
}

static int pickScanIndex(int16_t n, const String &ssid) {
  int best5 = -1;
  int best2 = -1;
  int32_t rssi5 = -127;
  int32_t rssi2 = -127;
  if (n <= 0) {
    return -1;
  }
  for (int i = 0; i < n; ++i) {
    const String seen = WiFi.SSID(i);
    if (seen != ssid && seen != s_pendingSsid && seen != s_savedSsid) {
      continue;
    }
    const int32_t rssi = WiFi.RSSI(i);
    if (WiFi.channel(i) > 14) {
      if (best5 < 0 || rssi > rssi5) {
        best5 = i;
        rssi5 = rssi;
      }
    } else if (best2 < 0 || rssi > rssi2) {
      best2 = i;
      rssi2 = rssi;
    }
  }
  if (s_bandPref == WifiBandPref::FiveG) {
    return best5;
  }
  if (s_bandPref == WifiBandPref::TwoG) {
    return best2;
  }
  return best5 >= 0 ? best5 : best2;
}

static void startPickBand(const String &ssid, const String &pass) {
  s_pendingSsid = ssid;
  s_savedPass = pass;
  s_pickBand = true;
  startScan(s_bandPref == WifiBandPref::TwoG ? WifiBandPref::TwoG
                                            : WifiBandPref::FiveG);
}

static void finishPickBand(int16_t n);

static void handleConnect() {
  if (s_scanRunning || s_pickBand) {
    sendJson(409, "{\"error\":\"scan in progress\"}");
    return;
  }
  String ssid = s_server.arg("ssid");
  ssid.trim();
  const String incomingPass = s_server.arg("password");
  if (ssid.length() == 0 || ssid.length() > 32) {
    sendJson(400, "{\"error\":\"bad ssid\"}");
    return;
  }
  if (incomingPass.length() > 0 && incomingPass.length() < 8) {
    sendJson(400, "{\"error\":\"password must be 8+ characters\"}");
    return;
  }
  if (incomingPass.length() > 63) {
    sendJson(400, "{\"error\":\"password too long\"}");
    return;
  }
  if (s_server.hasArg("band") && !setBandPref(s_server.arg("band"))) {
    sendJson(400, "{\"error\":\"bad band\"}");
    return;
  }
  String pass = incomingPass;
  if (pass.length() == 0 && ssid == s_savedSsid) {
    pass = s_savedPass;
  }
  const bool writePass = incomingPass.length() > 0 || ssid != s_savedSsid;
  uint8_t bssid[6];
  const bool haveBssid = parseBssid(s_server.arg("bssid"), bssid);
  const int32_t ch = parseChannelArg(s_server.arg("ch"));
  if (s_server.hasArg("ch") && s_server.arg("ch").length() && ch < 0) {
    sendJson(400, "{\"error\":\"bad ch\"}");
    return;
  }
  LOG_V("http", "connect ssid=%s", ssid.c_str());
  const String prevSsid = s_savedSsid;
  saveCreds(ssid, pass, writePass);
  if (ssid != prevSsid) {
    clearSavedBssid();
  }
  if (postedBssidOk(s_bandPref, haveBssid, ch)) {
    const uint8_t ghz = ch > 14 ? 5 : 2;
    saveBssid(bssid, ch > 0 ? ch : 0, ghz);
    beginConnect(ssid, pass, ch > 0 ? ch : 0, bssid);
  } else {
    startPickBand(ssid, pass);
  }
  sendStatus(200);
}

static void handleBand() {
  if (!s_server.hasArg("band") || !setBandPref(s_server.arg("band"))) {
    sendJson(400, "{\"error\":\"bad band\"}");
    return;
  }
  sendStatus(200);
}

// POST /distribute: path of a full show on this SD. Slices it for every
// cue-bus peer's patch and uploads the slices (background; /status dist).
static void handleDistribute() {
  if (LiveInput::active() && !Playback::hold()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  String path = s_server.arg("path");
  path.trim();
  if (!validUploadPath(path.c_str())) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const char *err = nullptr;
  if (!Distribute::start(path.c_str(), err)) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(strcmp(err ? err : "", "busy") == 0 ? 409 : 400, out);
    return;
  }
  sendStatus(200);
}

// POST /stream: on=1 plays the full show at path here and streams every
// cue-bus peer its universes live; on=0 stops streaming.
static void handleStream() {
  if (s_server.arg("on") == "0") {
    StreamTx::stop();
    sendStatus(200);
    return;
  }
  if (LiveInput::active() && !Playback::hold()) {
    sendJson(503, "{\"error\":\"live\"}");
    return;
  }
  String path = s_server.arg("path");
  path.trim();
  if (!validUploadPath(path.c_str())) {
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  if (!PlayCfg::set(PlaySrc::File, path.c_str(), PlayFileLoop::One,
                    PlayCfg::folderRep(), PlayCfg::folderN(), false, false)) {
    sendJson(400, "{\"error\":\"bad play\"}");
    return;
  }
  Sync::playUngrouped();
  Playback::reload();
  Playback::play();
  const char *err = nullptr;
  if (!StreamTx::start(path.c_str(), err)) {
    String out = "{\"error\":";
    jsonEscape(out, String(err ? err : "failed"));
    out += '}';
    sendJson(409, out);
    return;
  }
  sendStatus(200);
}

// POST /shownet: role standalone|host|member, ssid, pass (empty keeps the
// saved one for the same SSID), ch 1-13. Persists and reboots.
static void handleShowNet() {
  ShowRole role = ShowRole::Standalone;
  if (!ShowNet::parseRole(s_server.arg("role").c_str(), role)) {
    sendJson(400, "{\"error\":\"bad role\"}");
    return;
  }
  String ssid = s_server.arg("ssid");
  String pass = s_server.arg("pass");
  ssid.trim();
  const long ch = s_server.hasArg("ch") ? s_server.arg("ch").toInt() : 6;
  if (role != ShowRole::Standalone) {
    if (!ssid.length() || ssid.length() > 32) {
      sendJson(400, "{\"error\":\"bad ssid\"}");
      return;
    }
    if (!pass.length() && ssid == ShowNet::ssid()) {
      pass = ShowNet::pass();
    }
    if (pass.length() && (pass.length() < 8 || pass.length() > 63)) {
      sendJson(400, "{\"error\":\"password 8-63\"}");
      return;
    }
    if (ch < 1 || ch > 13) {
      sendJson(400, "{\"error\":\"bad channel\"}");
      return;
    }
  }
  if (!ShowNet::save(role, ssid.c_str(), pass.c_str(), static_cast<uint8_t>(ch))) {
    sendJson(500, "{\"error\":\"save failed\"}");
    return;
  }
  LOG_V("http", "shownet role=%s ssid=%s", ShowNet::roleName(), ShowNet::ssid());
  sendStatus(200);
  armReboot();
}

static void handleForget() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  clearCreds();
  s_pendingSsid = "";
  s_pickBand = false;
  s_havePendingBssid = false;
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

static void finishPickBand(int16_t n) {
  s_pickBand = false;
  const String ssid = s_pendingSsid.length() ? s_pendingSsid : s_savedSsid;
  const String pass = s_savedPass;
  const int best = pickScanIndex(n, ssid);
  if (best >= 0) {
    const uint8_t *mac = WiFi.BSSID(best);
    const int32_t ch = WiFi.channel(best);
    if (mac != nullptr) {
      saveBssid(mac, ch, ch > 14 ? 5 : 2);
      beginConnect(ssid, pass, ch, mac);
      return;
    }
  }
  if (s_bandPref == WifiBandPref::TwoG) {
    LOG_V("wifi", "pick 2g ssid-only ssid=%s", ssid.c_str());
    beginConnect(ssid, pass, 0, nullptr);
    return;
  }
  LOG_C("wifi", "pick failed pref=%s ssid=%s", bandPrefName(s_bandPref),
        ssid.c_str());
  s_pendingSsid = ssid;
  failConnect("no network found");
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
    if (s_pickBand) {
      finishPickBand(-1);
    }
    return;
  }
  s_haveScan = true;
  LOG_V("wifi", "scan n=%d", n);
  if (s_pickBand) {
    finishPickBand(n);
  }
}

static void pollConnect() {
  if (s_gotIp) {
    s_gotIp = false;
    uint8_t mac[6];
    uint8_t *bssid = WiFi.BSSID(mac);
    const int32_t ch = WiFi.channel();
    const uint8_t ghz = ch > 14 ? 5 : 2;
    // The show network is the Show Host's own 2.4 GHz AP: no band policy, and
    // it never replaces the saved venue network's BSSID.
    const bool onShow = ShowNet::isMember() && WiFi.SSID() == ShowNet::ssid();
    const bool wrongFive = s_bandPref == WifiBandPref::FiveG && ghz != 5;
    const bool wrongTwo = s_bandPref == WifiBandPref::TwoG && ghz == 5;
    if ((wrongFive || wrongTwo) && !s_pickBand && !onShow) {
      LOG_C("wifi", "wrong band pref=%s link=%ug", bandPrefName(s_bandPref),
            static_cast<unsigned>(ghz));
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false);
      const String ssid =
          s_pendingSsid.length() ? s_pendingSsid : s_savedSsid;
      startPickBand(ssid, s_savedPass);
    } else {
      s_connectStatus = ConnectStatus::Connected;
      s_error[0] = '\0';
      LOG_V("wifi", "connected ssid=%s ip=%s", WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str());
      if (bssid != nullptr && !onShow) {
        saveBssid(bssid, ch, ghz);
      }
      s_staIpPending = true;
    }
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
#if CONFIG_IDF_TARGET_ESP32C5
  phy_bbpll_en_usb(true);
#endif
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
#if CONFIG_IDF_TARGET_ESP32C5
  phy_bbpll_en_usb(true);
#endif
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(kApSsid);
  WiFi.onEvent(onWifiEvent);

  ShowNet::begin();
  if (ShowNet::isHost()) {
    SyncNet::setRole(SyncRole::Host);
  }
  loadBandPref();
  startAp();

  s_server.on("/", HTTP_GET, sendPage);
  s_server.on("/scan", HTTP_GET, handleScan);
  s_server.on("/status", HTTP_GET, handleStatus);
  s_server.on("/api/stats", HTTP_GET, handleStats);
  s_server.on("/connect", HTTP_POST, handleConnect);
  s_server.on("/band", HTTP_POST, handleBand);
  s_server.on("/forget", HTTP_POST, handleForget);
  s_server.on("/shownet", HTTP_POST, handleShowNet);
  s_server.on("/distribute", HTTP_POST, handleDistribute);
  s_server.on("/stream", HTTP_POST, handleStream);
  s_server.on("/brightness", HTTP_POST, handleBrightness);
  s_server.on("/pins", HTTP_POST, handlePins);
  s_server.on("/map", HTTP_POST, handleMap);
  s_server.on("/identify", HTTP_POST, handleIdentify);
  s_server.on("/test", HTTP_POST, handleTest);
  s_server.on("/reboot", HTTP_POST, handleReboot);
  s_server.on("/live", HTTP_POST, handleLive);
  s_server.on("/play", HTTP_POST, handlePlay);
  s_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadFile);
  s_server.on("/ota", HTTP_POST, handleOtaDone, handleOtaFile);
  s_server.on("/ota/peers", HTTP_POST, handleOtaPeers);
  s_server.on("/fixture", HTTP_GET, handleFixtureGet);
  s_server.on("/fixture", HTTP_POST, handleFixturePost);
  s_server.on("/fixture/names", HTTP_GET, handleFixtureNamesGet);
  s_server.on("/fixture/names", HTTP_POST, handleFixtureNamesPost);
  s_server.on("/fixture/locate", HTTP_POST, handleFixtureLocate);
  s_server.on("/name", HTTP_POST, handleName);
  s_server.on("/rename", HTTP_POST, handleRename);
  s_server.on("/meta", HTTP_POST, handleMeta);
  s_server.on("/delete", HTTP_POST, handleDelete);
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
  static const char *kHdrs[] = {"If-None-Match"};
  s_server.collectHeaders(kHdrs, 1);
  s_server.begin();
  LOG_V("http", "listen :%u", kHttpPort);

  Preferences prefs;
  String ssid;
  String pass;
  String bssidStr;
  int32_t ch = 0;
  uint8_t ghz = 0;
  if (prefs.begin(kPrefsNs, true)) {
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    bssidStr = prefs.getString("bssid", "");
    ch = prefs.getInt("ch", 0);
    ghz = prefs.getUChar("ghz", 0);
    prefs.end();
  }
  s_savedPass = pass;
  if (parseBssid(bssidStr, s_savedBssid)) {
    s_savedCh = ch;
    s_savedGhz = ghz;
    s_haveSavedBssid = true;
  } else {
    clearSavedBssidMem();
  }
  if (ssid.length()) {
    s_savedSsid = ssid;
  }
  if (ShowNet::isMember()) {
    // The show network first; the venue network is the fallback.
    s_memberOnShow = true;
    beginConnect(String(ShowNet::ssid()), String(ShowNet::pass()), 0, nullptr);
  } else if (ssid.length()) {
    LOG_V("wifi", "saved ssid=%s", ssid.c_str());
    const bool savedOk =
        s_haveSavedBssid && postedBssidOk(s_bandPref, true, s_savedCh);
    if (savedOk) {
      beginConnect(ssid, pass, s_savedCh, s_savedBssid);
    } else {
      startPickBand(ssid, pass);
    }
  } else {
    s_savedSsid = "";
    LOG_V("wifi", "no saved creds");
  }
}

void WifiSetup::saveCredentials(const char *ssid, const char *pass) {
  saveCreds(String(ssid ? ssid : ""), String(pass ? pass : ""), true);
}

const char *WifiSetup::savedSsid() { return s_savedSsid.c_str(); }

void WifiSetup::service() {
  if (!s_connectTimerArmed) {
    s_connectTimerArmed = true;
    if (s_connectStatus == ConnectStatus::Connecting) {
      s_connectStart = millis();
    }
  }
  pollScan();
  pollConnect();
  if (ShowNet::isMember() && s_connectStatus == ConnectStatus::Failed &&
      s_memberRetryAt != 0 &&
      static_cast<int32_t>(millis() - s_memberRetryAt) >= 0) {
    s_memberRetryAt = 0;
    s_memberOnShow = !s_memberOnShow || !s_savedSsid.length();
    if (s_memberOnShow) {
      beginConnect(String(ShowNet::ssid()), String(ShowNet::pass()), 0, nullptr);
    } else {
      beginConnect(s_savedSsid, s_savedPass, 0, nullptr);
    }
  }
  if (!ShowNet::isHost() && LiveCfg::park() && WiFi.status() == WL_CONNECTED) {
    stopAp();
  } else {
    startAp();
  }
  if (s_staIpPending) {
    s_staIpPending = false;
    if (WiFi.status() == WL_CONNECTED) {
      LiveInput::onStaGotIp();
    }
  }
  if (s_dnsUp) {
    s_dns.processNextRequest();
  }
  s_server.handleClient();
  Ota::service(s_apUp || WiFi.status() == WL_CONNECTED);
  LedBus::service();
  if (s_rebootAt != 0 && static_cast<int32_t>(millis() - s_rebootAt) >= 0) {
    s_rebootAt = 0;
    ESP.restart();
  }
}
