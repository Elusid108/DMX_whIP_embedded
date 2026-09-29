#include "serial_cmd.h"

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_profile.h"
#include "button.h"
#include "identify.h"
#include "led_bus.h"
#include "led_ctrl.h"
#include "led_test.h"
#include "live_input.h"
#include "log.h"
#include "json_lite.h"
#include "node_id.h"
#include "platform.h"
#include "pixel_map.h"
#include "play_cfg.h"
#include "playback.h"
#include "sd_info.h"
#include "version.h"
#if WHIP_HAS_NET
#include "wifi_setup.h"
#endif

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

// Standard base64 (padding optional). False on a bad character or overflow.
static bool base64Decode(const char *in, uint8_t *out, size_t cap, size_t &n) {
  uint32_t acc = 0;
  int bits = 0;
  n = 0;
  for (const char *p = in; *p && *p != '='; ++p) {
    const char c = *p;
    int v;
    if (c >= 'A' && c <= 'Z') {
      v = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      v = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      v = c - '0' + 52;
    } else if (c == '+') {
      v = 62;
    } else if (c == '/') {
      v = 63;
    } else {
      return false;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= cap) {
        return false;
      }
      out[n++] = static_cast<uint8_t>(acc >> bits);
    }
  }
  return true;
}

static void appendId(String &out) {
  char mac[18];
  Platform::uniqueId(mac, sizeof(mac));
  out += ",\"tag\":";
  putStr(out, kWhipFwTag);
  out += ",\"board\":";
  putStr(out, BoardProfile::id());
  out += ",\"chip\":";
  putStr(out, BoardProfile::chip());
#if defined(ARDUINO_ARCH_RP2040)
  out += ",\"fam\":\"rp\",\"ver\":";
#else
  out += ",\"fam\":\"esp\",\"ver\":";
#endif
  putStr(out, kFirmwareVersion);
  out += ",\"api\":";
  out += static_cast<unsigned>(kFirmwareApi);
  out += ",\"name\":";
  putStr(out, NodeId::longName());
  out += ",\"short\":";
  putStr(out, NodeId::shortName());
  out += ",\"mac\":";
  putStr(out, mac);
  out += WHIP_HAS_NET ? ",\"net\":true" : ",\"net\":false";
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
  out += ",\"btn\":";
  if (BoardProfile::buttonPin() == kGpioUnset) {
    out += "null";
  } else {
    out += static_cast<unsigned>(BoardProfile::buttonPin());
  }
#if WHIP_HAS_NET
  out += ",\"ssid\":";
  putStr(out, WifiSetup::savedSsid());
#endif
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
  // btn: GPIO number, or -1 / 255 for none. Absent = unchanged.
  const bool hasBtn = NetHttp::findKey(json, end, "btn") != nullptr;
  const long btn = NetHttp::jsonInt(json, end, "btn", -1);
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
  if (hasBtn) {
    const uint8_t pin = (btn < 0 || btn > 254) ? kGpioUnset : static_cast<uint8_t>(btn);
    if (!BoardProfile::setButtonPin(pin, true)) {
      fail("set", "bad btn");
      return;
    }
    Button::begin();
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
#if !WHIP_HAS_NET
  (void)json;
  fail("wifi", "no radio");
  return;
#else
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
#endif
}

static void cmdPmap(const char *b64) {
  if (LiveInput::active()) {
    fail("pmap", "live");
    return;
  }
  uint8_t raw[2 + 24 * 19];
  size_t n = 0;
  if (!base64Decode(b64, raw, sizeof(raw), n) || !PixelMap::setBlob(raw, n, true)) {
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

// test <rainbow|cycle|ends|off> [output]: the portal's LED test patterns,
// for boards with no portal and for checking wiring without an SD card.
static void cmdTest(char *arg) {
  char *outArg = arg;
  while (*outArg && *outArg != ' ') {
    ++outArg;
  }
  if (*outArg) {
    *outArg++ = '\0';
  }
  LedTestMode mode = LedTestMode::Off;
  if (strcmp(arg, "rainbow") == 0) {
    mode = LedTestMode::Rainbow;
  } else if (strcmp(arg, "cycle") == 0) {
    mode = LedTestMode::Cycle;
  } else if (strcmp(arg, "ends") == 0) {
    mode = LedTestMode::Ends;
  } else if (strcmp(arg, "off") != 0) {
    fail("test", "bad mode");
    return;
  }
  const long out = *outArg ? strtol(outArg, nullptr, 10) : 0;
  if (out < 0 || out >= PixelMap::outputCount() || out >= kPatchMaxOutputs) {
    fail("test", "bad output");
    return;
  }
  if (LiveInput::active()) {
    fail("test", "live");
    return;
  }
  if (mode != LedTestMode::Off) {
    Identify::cancel();
  }
  if (!LedTest::set(static_cast<uint8_t>(out), mode)) {
    fail("test", "bad output");
    return;
  }
  if (LedTest::active() && Playback::playing()) {
    Playback::pause();
  }
  String reply;
  begin(reply, "test", true);
  reply += ",\"mode\":";
  putStr(reply, LedTest::modeName(static_cast<uint8_t>(out)));
  reply += ",\"out\":";
  reply += static_cast<unsigned>(out);
  reply += ",\"px\":";
  reply += static_cast<unsigned>(PixelMap::outputPixelCount(static_cast<uint8_t>(out)));
  reply += ",\"pin\":";
  reply += static_cast<unsigned>(
      PixelMap::segment(PixelMap::firstSegmentOfOutput(static_cast<uint8_t>(out))).dataGpio);
  send(reply);
}

static void cmdReboot() {
  String out;
  begin(out, "reboot", true);
  send(out);
  Serial.flush();
  delay(100);
  Platform::restart();
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
  } else if (strcmp(cmd, "test") == 0) {
    cmdTest(arg);
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
