#include "sacn_rx.h"

#include "live_input.h"
#include "log.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

namespace {

static constexpr size_t kMaxPkt = 638;
static constexpr uint32_t kStatMs = 5000;
static constexpr uint32_t kRootData = 0x00000004;
static constexpr uint8_t kAcnId[12] = {'A', 'S', 'C', '-', 'E', '1',
                                       '.', '1', '7', 0,   0,   0};
static constexpr uint32_t kFramingData = 0x00000002;
static constexpr int kMinPkt = 126;
static constexpr int kDmpValuesOff = 125;
static constexpr int kDmxOff = 126;
static constexpr uint16_t kMaxDmx = 512;
static constexpr uint16_t kDestRgb = 192;  // 64 pixels

static WiFiUDP s_udp;
static uint8_t s_pkt[kMaxPkt];
static uint8_t s_seq = 0;
static uint32_t s_statMs = 0;
static uint32_t s_pkts = 0;
static uint32_t s_dmxOk = 0;
static uint32_t s_wrongUni = 0;
static bool s_up = false;
static bool s_mcast = false;
static bool s_loggedFirst = false;

static uint16_t rd16(const uint8_t *p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

static IPAddress multicastGroup(uint16_t uni) {
  return IPAddress(kSacnMcastA, kSacnMcastB, sacnMcastC(uni), sacnMcastD(uni));
}

static bool joinMcast() {
  if (!s_up || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const IPAddress group = multicastGroup(kSacnUniverse);
  s_udp.stop();
  if (!s_udp.beginMulticast(group, kSacnPort)) {
    LOG_C("sacn", "mcast join failed");
    if (!s_udp.begin(kSacnPort)) {
      s_up = false;
      return false;
    }
    s_mcast = false;
    return false;
  }
  s_mcast = true;
  LOG_V("sacn", "mcast %s:%u uni=%u", group.toString().c_str(), kSacnPort,
        kSacnUniverse);
  return true;
}

static void parsePacket(int n, const IPAddress &from) {
  ++s_pkts;
  if (n < kMinPkt) {
    return;
  }
  if (memcmp(s_pkt + 4, kAcnId, sizeof(kAcnId)) != 0) {
    return;
  }
  if (rd32(s_pkt + 18) != kRootData) {
    return;
  }
  if (rd32(s_pkt + 40) != kFramingData) {
    return;
  }
  if (s_pkt[117] != 0x02) {
    return;
  }
  const uint8_t options = s_pkt[112];
  if (options & 0x80) {
    return;
  }
  const uint16_t uni = rd16(s_pkt + 113);
  if (uni != kSacnUniverse) {
    ++s_wrongUni;
    return;
  }
  const uint16_t propCount = rd16(s_pkt + 123);
  if (propCount < 2) {
    return;
  }

  // DMP property values (start code + slots) begin at kDmpValuesOff.
  // Copy at most propCount bytes from that layer; ignore trailing junk.
  const uint16_t pktAvail = static_cast<uint16_t>(n - kDmpValuesOff);
  uint16_t take = propCount;
  if (take > pktAvail) {
    take = pktAvail;
  }
  if (take < 2) {
    return;
  }
  if (s_pkt[kDmpValuesOff] != 0) {
    return;
  }

  uint16_t len = static_cast<uint16_t>(take - 1);
  if (len > kMaxDmx) {
    len = kMaxDmx;
  }
  if (len > kDestRgb) {
    len = kDestRgb;
  }

  s_seq = s_pkt[111];
  if (!LiveInput::push(LiveSource::Sacn, s_pkt + kDmxOff, len)) {
    return;
  }
  ++s_dmxOk;
  if (!s_loggedFirst) {
    s_loggedFirst = true;
    LOG_V("sacn", "dmx uni=%u seq=%u len=%u from=%s", uni, s_seq, len,
          from.toString().c_str());
  }
}

} // namespace

void SacnRx::begin() {
  if (s_up) {
    return;
  }
  if (!s_udp.begin(kSacnPort)) {
    LOG_C("sacn", "udp bind :%u failed", kSacnPort);
    return;
  }
  s_up = true;
  s_mcast = false;
  LOG_V("sacn", "listen :%u uni=%u", kSacnPort, kSacnUniverse);
  joinMcast();
}

void SacnRx::stop() {
  if (!s_up) {
    return;
  }
  s_udp.stop();
  s_up = false;
  s_mcast = false;
  LOG_V("sacn", "stop");
}

void SacnRx::onStaGotIp() {
  if (!s_up) {
    return;
  }
  joinMcast();
}

void SacnRx::service() {
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
    if (s_pkts > 0 || s_dmxOk > 0) {
      LOG_V("sacn", "rx pkts=%u dmx=%u uni=%u seq=%u skip_uni=%u",
            static_cast<unsigned>(s_pkts), static_cast<unsigned>(s_dmxOk),
            kSacnUniverse, s_seq, static_cast<unsigned>(s_wrongUni));
    }
  }
}
