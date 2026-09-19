#include "sacn_rx.h"

#include "live_input.h"
#include "log.h"
#include "pixel_map.h"
#include "sync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

#include "lwip/igmp.h"
#include "lwip/ip4_addr.h"

namespace {

static constexpr size_t kMaxPkt = 638;
static constexpr uint32_t kStatMs = 5000;
static constexpr uint32_t kRootData = 0x00000004;
static constexpr uint32_t kRootExtended = 0x00000008;
static constexpr uint8_t kAcnId[12] = {'A', 'S', 'C', '-', 'E', '1',
                                       '.', '1', '7', 0,   0,   0};
static constexpr uint32_t kFramingData = 0x00000002;
static constexpr uint32_t kFramingSync = 0x00000001;
static constexpr int kMinSync = 49;
static constexpr int kMinPkt = 126;
static constexpr int kDmpValuesOff = 125;
static constexpr int kDmxOff = 126;
static constexpr uint16_t kMaxDmx = 512;
static WiFiUDP s_udp;
static uint8_t s_pkt[kMaxPkt];
static uint8_t s_seq = 0;
static uint32_t s_statMs = 0;
static uint32_t s_pkts = 0;
static uint32_t s_dmxOk = 0;
static uint32_t s_wrongUni = 0;
static uint32_t s_syncOk = 0;
static bool s_want = false;
static bool s_up = false;
static bool s_mcast = false;
static uint32_t s_bindMs = 0;
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
  uint16_t unis[kLiveUniSlots];
  const uint8_t n = PixelMap::collectSacnUniverses(unis, kLiveUniSlots);
  const uint16_t start = n ? unis[0] : PixelMap::firstSacnUniverse();
  const IPAddress group = multicastGroup(start);
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
  for (uint8_t i = 1; i < n; ++i) {
    const IPAddress extra = multicastGroup(unis[i]);
    ip4_addr_t addr;
    IP4_ADDR(&addr, extra[0], extra[1], extra[2], extra[3]);
    igmp_joingroup(IP4_ADDR_ANY4, &addr);
  }
  LOG_V("sacn", "mcast %s:%u uni=%u n=%u", group.toString().c_str(), kSacnPort,
        start, n);
  return true;
}

static void parsePacket(int n, const IPAddress &from) {
  ++s_pkts;
  if (n < 22) {
    return;
  }
  if (memcmp(s_pkt + 4, kAcnId, sizeof(kAcnId)) != 0) {
    return;
  }
  const uint32_t root = rd32(s_pkt + 18);
  if (root == kRootExtended) {
    if (n < kMinSync) {
      return;
    }
    if (rd32(s_pkt + 40) != kFramingSync) {
      return;
    }
    const uint16_t uni = rd16(s_pkt + 45);
    ++s_syncOk;
    Sync::onE131Sync(uni);
    return;
  }
  if (n < kMinPkt) {
    return;
  }
  if (root != kRootData) {
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
  if (!PixelMap::wantsSacn(uni)) {
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

  s_seq = s_pkt[111];
  if (!LiveInput::push(LiveSource::Sacn, s_pkt + kDmxOff, len, uni)) {
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
  s_want = true;
  if (s_up) {
    return;
  }
  if (!s_udp.begin(kSacnPort)) {
    LOG_C("sacn", "udp bind :%u failed", kSacnPort);
    return;
  }
  s_up = true;
  s_mcast = false;
  LOG_V("sacn", "listen :%u uni=%u", kSacnPort, PixelMap::firstSacnUniverse());
  joinMcast();
}

void SacnRx::stop() {
  s_want = false;
  if (!s_up) {
    return;
  }
  s_udp.stop();
  s_up = false;
  s_mcast = false;
  LOG_V("sacn", "stop");
}

void SacnRx::onStaGotIp() {
  if (!s_want) {
    return;
  }
  if (s_up) {
    s_udp.stop();
    s_up = false;
    s_mcast = false;
  }
  begin();
}

void SacnRx::service() {
  if (s_want && !s_up) {
    const uint32_t now = millis();
    if (now - s_bindMs >= 500) {
      s_bindMs = now;
      begin();
    }
  }
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
    if (s_pkts > 0 || s_dmxOk > 0 || s_syncOk > 0) {
      LOG_V("sacn", "rx pkts=%u dmx=%u sync=%u uni=%u seq=%u skip_uni=%u",
            static_cast<unsigned>(s_pkts), static_cast<unsigned>(s_dmxOk),
            static_cast<unsigned>(s_syncOk),
            PixelMap::firstSacnUniverse(), s_seq,
            static_cast<unsigned>(s_wrongUni));
    }
  }
}
