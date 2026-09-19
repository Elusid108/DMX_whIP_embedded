#include "artnet_rx.h"

#include "live_input.h"
#include "log.h"
#include "node_id.h"
#include "sync.h"
#include "version.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr size_t kHdr = 18;
static constexpr size_t kOpOff = 10;
static constexpr size_t kMaxPkt = 18 + 512;
static constexpr uint32_t kStatMs = 5000;
static constexpr uint8_t kStyleNode = 0x00;
static constexpr uint8_t kPortTypeDmxOut = 0x80;

static WiFiUDP s_udp;
static uint8_t s_pkt[kMaxPkt];
static uint8_t s_reply[kArtNetPollReplyLen];
static uint8_t s_seq = 0;
static uint32_t s_statMs = 0;
static uint32_t s_pkts = 0;
static uint32_t s_dmxOk = 0;
static uint32_t s_polls = 0;
static uint32_t s_replies = 0;
static uint32_t s_wrongUni = 0;
static uint32_t s_syncs = 0;
static bool s_up = false;
static bool s_loggedFirstDmx = false;
static bool s_loggedFirstPoll = false;

static bool ipIsZero(const IPAddress &ip) {
  return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool ipEq(const IPAddress &a, const IPAddress &b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static bool sameSubnet(const IPAddress &ip, const IPAddress &net,
                       const IPAddress &mask) {
  return ((ip[0] ^ net[0]) & mask[0]) == 0 &&
         ((ip[1] ^ net[1]) & mask[1]) == 0 &&
         ((ip[2] ^ net[2]) & mask[2]) == 0 &&
         ((ip[3] ^ net[3]) & mask[3]) == 0;
}

static IPAddress nodeIp(const IPAddress &from) {
  const IPAddress sta = WiFi.localIP();
  const IPAddress staMask = WiFi.subnetMask();
  const IPAddress ap = WiFi.softAPIP();
  const IPAddress apMask(255, 255, 255, 0);

  if (!ipIsZero(sta) && !ipIsZero(staMask) && sameSubnet(from, sta, staMask)) {
    return sta;
  }
  if (!ipIsZero(ap) && sameSubnet(from, ap, apMask)) {
    return ap;
  }
  if (!ipIsZero(sta)) {
    return sta;
  }
  return ap;
}

static IPAddress directedBcast(const IPAddress &ip) {
  IPAddress mask = WiFi.subnetMask();
  if (!ipIsZero(WiFi.softAPIP()) && ipEq(ip, WiFi.softAPIP())) {
    mask = IPAddress(255, 255, 255, 0);
  }
  if (ipIsZero(mask)) {
    return IPAddress(255, 255, 255, 255);
  }
  return IPAddress(static_cast<uint8_t>(ip[0] | static_cast<uint8_t>(~mask[0])),
                   static_cast<uint8_t>(ip[1] | static_cast<uint8_t>(~mask[1])),
                   static_cast<uint8_t>(ip[2] | static_cast<uint8_t>(~mask[2])),
                   static_cast<uint8_t>(ip[3] | static_cast<uint8_t>(~mask[3])));
}

static void fwVers(uint8_t &hi, uint8_t &lo) {
  hi = 0;
  lo = 0;
  const char *s = kFirmwareVersion;
  while (*s >= '0' && *s <= '9') {
    hi = static_cast<uint8_t>(hi * 10u + static_cast<uint8_t>(*s - '0'));
    ++s;
  }
  if (*s == '.') {
    ++s;
    while (*s >= '0' && *s <= '9') {
      lo = static_cast<uint8_t>(lo * 10u + static_cast<uint8_t>(*s - '0'));
      ++s;
    }
  }
}

static void putIp(uint8_t *p, const IPAddress &ip) {
  p[0] = ip[0];
  p[1] = ip[1];
  p[2] = ip[2];
  p[3] = ip[3];
}

static void buildPollReply(const IPAddress &ip) {
  memset(s_reply, 0, sizeof(s_reply));
  memcpy(s_reply, "Art-Net", 7);

  s_reply[8] = static_cast<uint8_t>(kArtNetOpPollReply & 0xFF);
  s_reply[9] = static_cast<uint8_t>(kArtNetOpPollReply >> 8);

  putIp(s_reply + 10, ip);

  s_reply[14] = static_cast<uint8_t>(kArtNetPort & 0xFF);
  s_reply[15] = static_cast<uint8_t>(kArtNetPort >> 8);

  uint8_t verH = 0;
  uint8_t verL = 0;
  fwVers(verH, verL);
  s_reply[16] = verH;
  s_reply[17] = verL;

  const uint16_t pa = kArtNetUniverse & 0x7FFF;
  s_reply[18] = static_cast<uint8_t>((pa >> 8) & 0x7F);
  s_reply[19] = static_cast<uint8_t>((pa >> 4) & 0x0F);
  s_reply[20] = 0x00;
  s_reply[21] = 0xFF;
  s_reply[23] = 0xC0;

  snprintf(reinterpret_cast<char *>(s_reply + 26), 18, "%s", NodeId::shortName());
  snprintf(reinterpret_cast<char *>(s_reply + 44), 64, "%s", NodeId::longName());
  snprintf(reinterpret_cast<char *>(s_reply + 108), 64,
           "#0001 [%04x] %s v%s", static_cast<unsigned>(pa), NodeId::shortName(),
           kFirmwareVersion);

  s_reply[173] = 1;
  s_reply[174] = kPortTypeDmxOut;
  s_reply[182] = 0x80;
  s_reply[190] = static_cast<uint8_t>(pa & 0x0F);
  s_reply[200] = kStyleNode;

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  memcpy(s_reply + 201, mac, 6);

  putIp(s_reply + 207, ip);
  s_reply[211] = 1;

  uint8_t status2 = 0x05;
  if (WiFi.status() == WL_CONNECTED) {
    status2 |= 0x02;
  }
  s_reply[212] = status2;
}

static bool sendReplyTo(const IPAddress &dest) {
  if (ipIsZero(dest)) {
    return false;
  }
  if (!s_udp.beginPacket(dest, kArtNetPort)) {
    return false;
  }
  const size_t wrote = s_udp.write(s_reply, kArtNetPollReplyLen);
  if (wrote != kArtNetPollReplyLen) {
    s_udp.endPacket();
    return false;
  }
  return s_udp.endPacket() == 1;
}

static void addDest(IPAddress *dests, uint8_t &n, const IPAddress &d) {
  if (ipIsZero(d) || n >= 3) {
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    if (ipEq(dests[i], d)) {
      return;
    }
  }
  dests[n++] = d;
}

static void sendPollReply(const IPAddress &from) {
  const IPAddress ip = nodeIp(from);
  if (ipIsZero(ip)) {
    LOG_V("artnet", "poll from=%s ignored (no ip)", from.toString().c_str());
    return;
  }

  buildPollReply(ip);

  IPAddress dests[3];
  uint8_t n = 0;
  addDest(dests, n, from);
  addDest(dests, n, directedBcast(ip));
  addDest(dests, n, IPAddress(255, 255, 255, 255));

  bool ok = false;
  bool uniOk = false;
  for (uint8_t i = 0; i < n; ++i) {
    if (sendReplyTo(dests[i])) {
      ok = true;
      if (ipEq(dests[i], from)) {
        uniOk = true;
      }
    } else if (ipEq(dests[i], from)) {
      LOG_C("artnet", "poll reply unicast failed dest=%s",
            from.toString().c_str());
    }
  }

  if (ok) {
    ++s_replies;
  }

  if (!s_loggedFirstPoll) {
    s_loggedFirstPoll = true;
    LOG_V("artnet", "poll from=%s reply ip=%s uni=%u dests=%u unicast=%u",
          from.toString().c_str(), ip.toString().c_str(), kArtNetUniverse, n,
          uniOk ? 1u : 0u);
  } else {
    LOG_V("artnet", "poll from=%s reply ip=%s", from.toString().c_str(),
          ip.toString().c_str());
  }
}

static void parsePacket(int n, const IPAddress &from) {
  ++s_pkts;
  if (n < static_cast<int>(kOpOff)) {
    return;
  }
  if (memcmp(s_pkt, "Art-Net", 7) != 0 || s_pkt[7] != 0) {
    return;
  }
  const uint16_t op = static_cast<uint16_t>(s_pkt[8] | (s_pkt[9] << 8));
  if (op == kArtNetOpPoll) {
    ++s_polls;
    sendPollReply(from);
    return;
  }
  if (op == kArtNetOpSync) {
    ++s_syncs;
    Sync::onArtSync();
    return;
  }
  if (op != kArtNetOpDmx) {
    return;
  }
  if (n < static_cast<int>(kHdr)) {
    return;
  }
  const uint16_t uni =
      static_cast<uint16_t>(s_pkt[14] | (s_pkt[15] << 8)) & 0x7FFF;
  if (uni != kArtNetUniverse) {
    ++s_wrongUni;
    return;
  }
  uint16_t len = static_cast<uint16_t>((s_pkt[16] << 8) | s_pkt[17]);
  if (len > 512) {
    len = 512;
  }
  if (kHdr + len > static_cast<size_t>(n)) {
    len = static_cast<uint16_t>(n - kHdr);
  }
  s_seq = s_pkt[12];
  if (!LiveInput::push(LiveSource::ArtNet, s_pkt + kHdr, len)) {
    return;
  }
  ++s_dmxOk;
  if (!s_loggedFirstDmx) {
    s_loggedFirstDmx = true;
    LOG_V("artnet", "dmx uni=%u seq=%u len=%u from=%s", uni, s_seq, len,
          from.toString().c_str());
  }
}

} // namespace

void ArtNetRx::begin() {
  if (s_up) {
    return;
  }
  if (!s_udp.begin(kArtNetPort)) {
    LOG_C("artnet", "udp bind :%u failed", kArtNetPort);
    return;
  }
  s_up = true;
  LOG_V("artnet", "listen :%u uni=%u", kArtNetPort, kArtNetUniverse);
}

void ArtNetRx::stop() {
  if (!s_up) {
    return;
  }
  s_udp.stop();
  s_up = false;
  LOG_V("artnet", "stop");
}

void ArtNetRx::onStaGotIp() {
  if (s_up) {
    s_udp.stop();
    s_up = false;
  }
  begin();
}

void ArtNetRx::service() {
  if (!s_up) {
    return;
  }
  for (;;) {
    const int n = s_udp.parsePacket();
    if (n <= 0) {
      break;
    }
    const IPAddress from = s_udp.remoteIP();
    const int got =
        s_udp.read(s_pkt, n > static_cast<int>(kMaxPkt) ? kMaxPkt : n);
    if (got > 0) {
      parsePacket(got, from);
    }
  }

  const uint32_t now = millis();
  if (now - s_statMs >= kStatMs) {
    s_statMs = now;
    if (s_pkts > 0 || s_dmxOk > 0 || s_polls > 0 || s_syncs > 0) {
      LOG_V("artnet",
            "rx pkts=%u dmx=%u sync=%u poll=%u reply=%u uni=%u seq=%u "
            "skip_uni=%u",
            static_cast<unsigned>(s_pkts), static_cast<unsigned>(s_dmxOk),
            static_cast<unsigned>(s_syncs), static_cast<unsigned>(s_polls),
            static_cast<unsigned>(s_replies), kArtNetUniverse, s_seq,
            static_cast<unsigned>(s_wrongUni));
    }
  }
}
