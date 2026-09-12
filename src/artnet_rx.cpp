#include "artnet_rx.h"

#include "log.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

namespace {

static constexpr uint16_t kOpDmx = 0x5000;
static constexpr size_t kHdr = 18;
static constexpr size_t kMaxPkt = 18 + 512;
static constexpr uint32_t kStatMs = 5000;

static WiFiUDP s_udp;
static uint8_t s_pkt[kMaxPkt];
static uint8_t s_dmx[512];
static uint16_t s_dmxLen = 0;
static uint8_t s_seq = 0;
static uint32_t s_lastMs = 0;
static uint32_t s_statMs = 0;
static uint32_t s_pkts = 0;
static uint32_t s_dmxOk = 0;
static uint32_t s_drops = 0;
static uint32_t s_wrongUni = 0;
static bool s_unread = false;
static bool s_loggedFirst = false;

static void takeDmx(const uint8_t *data, uint16_t len, uint8_t seq) {
  if (len > 512) {
    len = 512;
  }
  if (s_unread) {
    ++s_drops;
  }
  memcpy(s_dmx, data, len);
  if (len < 512) {
    memset(s_dmx + len, 0, 512 - len);
  }
  s_dmxLen = len;
  s_seq = seq;
  s_unread = true;
  s_lastMs = millis();
  ++s_dmxOk;
}

static void parsePacket(int n, const IPAddress &from) {
  ++s_pkts;
  if (n < static_cast<int>(kHdr)) {
    return;
  }
  if (memcmp(s_pkt, "Art-Net", 7) != 0 || s_pkt[7] != 0) {
    return;
  }
  const uint16_t op = static_cast<uint16_t>(s_pkt[8] | (s_pkt[9] << 8));
  if (op != kOpDmx) {
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
  const uint8_t seq = s_pkt[12];
  takeDmx(s_pkt + kHdr, len, seq);
  if (!s_loggedFirst) {
    s_loggedFirst = true;
    LOG_V("artnet", "dmx uni=%u seq=%u len=%u from=%s", uni, seq, len,
          from.toString().c_str());
  }
}

} // namespace

void ArtNetRx::begin() {
  memset(s_dmx, 0, sizeof(s_dmx));
  if (!s_udp.begin(kArtNetPort)) {
    LOG_C("artnet", "udp bind :%u failed", kArtNetPort);
    return;
  }
  LOG_V("artnet", "listen :%u uni=%u", kArtNetPort, kArtNetUniverse);
}

void ArtNetRx::service() {
  for (;;) {
    const int n = s_udp.parsePacket();
    if (n <= 0) {
      break;
    }
    const IPAddress from = s_udp.remoteIP();
    const int got = s_udp.read(s_pkt, n > static_cast<int>(kMaxPkt) ? kMaxPkt : n);
    if (got > 0) {
      parsePacket(got, from);
    }
  }

  const uint32_t now = millis();
  if (now - s_statMs >= kStatMs) {
    s_statMs = now;
    if (s_pkts > 0 || s_dmxOk > 0) {
      LOG_V("artnet", "rx pkts=%u dmx=%u uni=%u seq=%u drops=%u skip_uni=%u",
            static_cast<unsigned>(s_pkts), static_cast<unsigned>(s_dmxOk),
            kArtNetUniverse, s_seq, static_cast<unsigned>(s_drops),
            static_cast<unsigned>(s_wrongUni));
    }
  }
}

bool ArtNetRx::hasFresh(uint32_t timeoutMs) {
  if (s_lastMs == 0) {
    return false;
  }
  return (millis() - s_lastMs) < timeoutMs;
}

const uint8_t *ArtNetRx::dmx() { return s_dmx; }

uint16_t ArtNetRx::dmxLen() { return s_dmxLen; }

void ArtNetRx::consume() { s_unread = false; }
