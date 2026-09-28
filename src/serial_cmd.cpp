#include "serial_cmd.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_profile.h"
#include "led_bus.h"
#include "led_ctrl.h"
#include "led_test.h"
#include "live_input.h"
#include "log.h"
#include "mbedtls/base64.h"
#include "net_http.h"
#include "node_id.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "version.h"
#include "wifi_setup.h"

extern "C" const char kWhipFwTag[];

namespace {

// Largest request is pmap: 2 + 24 x 19 bytes = 458, ~612 base64 chars.
static constexpr size_t kLineMax = 1024;
static constexpr size_t kReplyMax = 768;

static char s_line[kLineMax];
static size_t s_len = 0;
static bool s_overflow = false;

// Minimal JSON string writer: quotes, backslash and control characters.
static void putStr(String &out, const char *s) {
  out += '"';
  for (const char *p = s ? s : ""; *p; ++p) {
    const char c = *p;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
      out += esc;
    } else {
      out += c;
    }
  }
  out += '"';
}

static void begin(String &out, const char *cmd, bool ok) {
  out.reserve(kReplyMax);
  out = "@whip {\"ok\":";
  out += ok ? "true" : "false";
  out += ",\"cmd\":";
  putStr(out, cmd);
}

static void send(String &out) {
  out += '}';
  Log::writeLine(out.c_str());
}

static void fail(const char *cmd, const char *error) {
  String out;
  begin(out, cmd, false);
  out += ",\"error\":";
  putStr(out, error);
  send(out);
}

static void macString(char *out, size_t n) {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static void appendId(String &out) {
  char mac[18];
  macString(mac, sizeof(mac));
  out += ",\"tag\":";
  putStr(out, kWhipFwTag);
  out += ",\"board\":";
  putStr(out, BoardProfile::id());
  out += ",\"chip\":";
  putStr(out, BoardProfile::chip());
  out += ",\"fam\":\"esp\",\"ver\":";
  putStr(out, kFirmwareVersion);
  out += ",\"api\":";
  out += static_cast<unsigned>(kFirmwareApi);
  out += ",\"name\":";
  putStr(out, NodeId::longName());
  out += ",\"short\":";
  putStr(out, NodeId::shortName());
  out += ",\"mac\":";
  putStr(out, mac);
  out += ",\"net\":true";
}

static void cmdId() {
  String out;
  begin(out, "id", true);
  appendId(out);
  send(out);
}

static void cmdGet() {
  String out;
  begin(out, "get", true);
  appendId(out);
  out += ",\"bri\":";
  out += static_cast<unsigned>(LedCtrl::get());
  char sd[80];
  snprintf(sd, sizeof(sd), ",\"sd\":{\"cs\":%u,\"mosi\":%u,\"clk\":%u,\"miso\":%u}",
           BoardProfile::sdCs(), BoardProfile::sdMosi(), BoardProfile::sdClk(),
           BoardProfile::sdMiso());
  out += sd;
  out += ",\"ssid\":";
  putStr(out, WifiSetup::savedSsid());
  out += ",\"play\":{\"src\":";
  putStr(out, PlayCfg::bootSrcName());
  out += ",\"path\":";
  putStr(out, PlayCfg::bootPath());
  out += '}';
  send(out);
}

static bool parseSrc(const char *s, PlaySrc &out) {
  if (strcmp(s, "root") == 0) {
    out = PlaySrc::Root;
  } else if (strcmp(s, "file") == 0) {
    out = PlaySrc::File;
  } else if (strcmp(s, "folder") == 0) {
    out = PlaySrc::Folder;
  } else if (strcmp(s, "none") == 0) {
    out = PlaySrc::None;
  } else {
    return false;
  }
  return true;
}

// Each field is optional; nothing is changed unless every present field is
// valid, except the order below (name, bri, sd, play) once validated.
static void cmdSet(const char *json) {
  const char *end = json + strlen(json);
  char name[NodeId::kLongMax + 1] = {0};
  char shortName[NodeId::kShortMax + 1] = {0};
  const bool hasName = NetHttp::jsonStr(json, end, "name", name, sizeof(name));
  const bool hasShort =
      NetHttp::jsonStr(json, end, "short", shortName, sizeof(shortName));
  const long bri = NetHttp::jsonInt(json, end, "bri", -1);
  const char *sd = NetHttp::findKey(json, end, "sd");
  const char *play = NetHttp::findKey(json, end, "play");

  if (bri != -1 && (bri < 0 || bri > 255)) {
    fail("set", "bad bri");
    return;
  }
  long pins[4] = {-1, -1, -1, -1};
  if (sd != nullptr) {
    static const char *const kKeys[4] = {"cs", "mosi", "clk", "miso"};
    const char *sdEnd = static_cast<const char *>(memchr(sd, '}', end - sd));
    if (sdEnd == nullptr) {
      fail("set", "bad sd");
      return;
    }
    for (uint8_t i = 0; i < 4; ++i) {
      pins[i] = NetHttp::jsonInt(sd, sdEnd, kKeys[i], -1);
      if (pins[i] < 0 || pins[i] > 255) {
        fail("set", "bad sd");
        return;
      }
    }
    if (LiveInput::active()) {
      fail("set", "live");
      return;
    }
  }
  PlaySrc src = PlaySrc::Root;
  char path[96] = {0};
  if (play != nullptr) {
    char srcName[12] = {0};
    if (!NetHttp::jsonStr(play, end, "src", srcName, sizeof(srcName)) ||
        !parseSrc(srcName, src)) {
      fail("set", "bad play");
      return;
    }
    NetHttp::jsonStr(play, end, "path", path, sizeof(path));
  }

  if ((hasName || hasShort) &&
      !NodeId::set(hasName ? name : NodeId::longName(),
                   hasShort ? shortName : nullptr, true)) {
    fail("set", "bad name");
    return;
  }
  if (bri != -1) {
    LedCtrl::set(static_cast<uint8_t>(bri), true);
  }
  if (sd != nullptr) {
    if (!BoardProfile::setSdPins(static_cast<uint8_t>(pins[0]),
                                 static_cast<uint8_t>(pins[1]),
                                 static_cast<uint8_t>(pins[2]),
                                 static_cast<uint8_t>(pins[3]), true)) {
      fail("set", "bad sd");
      return;
    }
    Playback::park();
    delay(80);
    SdInfo::remount();
  }
  if (play != nullptr &&
      !PlayCfg::setStartup(src, path, PlayCfg::bootFileLoop(),
                           PlayCfg::bootFolderRep(), PlayCfg::bootFolderN())) {
    fail("set", "bad play");
    return;
  }
  cmdGet();
}

static void cmdWifi(const char *json) {
  const char *end = json + strlen(json);
  char ssid[33] = {0};
  char pass[65] = {0};
  if (!NetHttp::jsonStr(json, end, "ssid", ssid, sizeof(ssid))) {
    fail("wifi", "bad ssid");
    return;
  }
  NetHttp::jsonStr(json, end, "pass", pass, sizeof(pass));
  WifiSetup::saveCredentials(ssid, pass);
  String out;
  begin(out, "wifi", true);
  out += ",\"ssid\":";
  putStr(out, ssid);
  out += ",\"reboot\":true";
  send(out);
}

static void cmdPmap(const char *b64) {
  if (LiveInput::active()) {
    fail("pmap", "live");
    return;
  }
  uint8_t raw[2 + 24 * 19];
  size_t n = 0;
  if (mbedtls_base64_decode(raw, sizeof(raw), &n,
                            reinterpret_cast<const unsigned char *>(b64),
                            strlen(b64)) != 0 ||
      !PixelMap::setBlob(raw, n, true)) {
    fail("pmap", "bad map");
    return;
  }
  LedBus::requestApply();
  LiveInput::applyCfg();
  LedTest::cancel();
  String out;
  begin(out, "pmap", true);
  out += ",\"segs\":";
  out += static_cast<unsigned>(PixelMap::segmentCount());
  out += ",\"px\":";
  out += static_cast<unsigned>(PixelMap::totalPixels());
  send(out);
}

static void cmdQuiet(const char *arg) {
  const bool on = arg[0] != '0';
  Log::setQuiet(on);
  String out;
  begin(out, "quiet", true);
  out += ",\"quiet\":";
  out += on ? "true" : "false";
  send(out);
}

static void cmdReboot() {
  String out;
  begin(out, "reboot", true);
  send(out);
  Serial.flush();
  delay(100);
  ESP.restart();
}

static void runLine(char *line) {
  while (*line == ' ') {
    ++line;
  }
  if (strncmp(line, "whip", 4) != 0 || (line[4] != ' ' && line[4] != '\0')) {
    return;
  }
  char *cmd = line + 4;
  while (*cmd == ' ') {
    ++cmd;
  }
  char *arg = cmd;
  while (*arg && *arg != ' ') {
    ++arg;
  }
  if (*arg) {
    *arg++ = '\0';
    while (*arg == ' ') {
      ++arg;
    }
  }
  if (strcmp(cmd, "id") == 0) {
    cmdId();
  } else if (strcmp(cmd, "get") == 0) {
    cmdGet();
  } else if (strcmp(cmd, "set") == 0) {
    cmdSet(arg);
  } else if (strcmp(cmd, "wifi") == 0) {
    cmdWifi(arg);
  } else if (strcmp(cmd, "pmap") == 0) {
    cmdPmap(arg);
  } else if (strcmp(cmd, "quiet") == 0) {
    cmdQuiet(arg);
  } else if (strcmp(cmd, "reboot") == 0) {
    cmdReboot();
  } else {
    fail(cmd, "unknown command");
  }
}

} // namespace

void SerialCmd::service() {
  // Bounded per call so a flood cannot stall the loop.
  for (int budget = 256; budget > 0 && Serial.available() > 0; --budget) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (s_overflow) {
        fail("line", "too long");
      } else if (s_len > 0) {
        s_line[s_len] = '\0';
        runLine(s_line);
      }
      s_len = 0;
      s_overflow = false;
      continue;
    }
    if (s_len + 1 < kLineMax) {
      s_line[s_len++] = static_cast<char>(c);
    } else {
      s_overflow = true;
    }
  }
}
