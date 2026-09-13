#include "pixel_map.h"

#include "log.h"

namespace {

static bool s_logged = false;

static PixelChan makeChan(const PixelMapCfg &m, uint16_t uniOff, uint16_t ch) {
  PixelChan c;
  c.artNetUniverse = static_cast<uint16_t>(m.startArtNetUniverse + uniOff);
  c.sacnUniverse = static_cast<uint16_t>(m.startSacnUniverse + uniOff);
  c.channel = ch;
  return c;
}

static bool mapPacked(const PixelMapCfg &m, uint16_t pixelIndex,
                      PixelRgbAddr &out) {
  const uint32_t base = static_cast<uint32_t>(m.startChannel - 1) +
                        static_cast<uint32_t>(pixelIndex) * m.channelsPerPixel;
  PixelChan *parts[3] = {&out.r, &out.g, &out.b};
  for (uint8_t c = 0; c < 3; ++c) {
    const uint32_t abs0 = base + c;
    const uint16_t uniOff =
        static_cast<uint16_t>(abs0 / kDmxUniverseSize);
    const uint16_t ch =
        static_cast<uint16_t>(abs0 % kDmxUniverseSize) + 1;
    *parts[c] = makeChan(m, uniOff, ch);
  }
  return true;
}

static bool mapWholePixels(const PixelMapCfg &m, uint16_t pixelIndex,
                           PixelRgbAddr &out) {
  const uint8_t chPer = m.channelsPerPixel;
  const uint16_t firstSlots =
      static_cast<uint16_t>(kDmxUniverseSize - (m.startChannel - 1));
  const uint16_t pixFirst = static_cast<uint16_t>(firstSlots / chPer);
  uint16_t uniOff;
  uint16_t chR;
  if (pixelIndex < pixFirst) {
    uniOff = 0;
    chR = static_cast<uint16_t>(m.startChannel +
                                static_cast<uint16_t>(pixelIndex) * chPer);
  } else {
    const uint16_t pixPerUni =
        static_cast<uint16_t>(kDmxUniverseSize / chPer);
    if (pixPerUni == 0) {
      return false;
    }
    const uint16_t rem = static_cast<uint16_t>(pixelIndex - pixFirst);
    uniOff = static_cast<uint16_t>(1 + rem / pixPerUni);
    chR = static_cast<uint16_t>(1 + (rem % pixPerUni) * chPer);
  }
  out.r = makeChan(m, uniOff, chR);
  out.g = makeChan(m, uniOff, static_cast<uint16_t>(chR + 1));
  out.b = makeChan(m, uniOff, static_cast<uint16_t>(chR + 2));
  return true;
}

} // namespace

void PixelMap::begin() {
  if (s_logged) {
    return;
  }
  s_logged = true;
  const PixelMapCfg &m = cfg();
  LOG_V("pmap",
        "chip=%s order=%s data=%u clk=%u count=%u artnet=%u sacn=%u "
        "start_ch=%u chips=%u ch_px=%u split=%u bri=%u",
        chipsetName(), colorOrderName(), m.dataGpio, m.clockGpio, m.pixelCount,
        m.startArtNetUniverse, m.startSacnUniverse, m.startChannel,
        m.chipsPerPixel, m.channelsPerPixel,
        static_cast<unsigned>(m.splitAcrossUniverses), m.brightnessDefault);
}

const PixelMapCfg &PixelMap::cfg() { return kMatrixPixelMap; }

uint16_t PixelMap::channelCount() {
  const PixelMapCfg &m = cfg();
  return static_cast<uint16_t>(m.pixelCount * m.channelsPerPixel);
}

const char *PixelMap::chipsetName() {
  switch (cfg().chipset) {
  case LedChipset::WS2812B:
  default:
    return "ws2812b";
  }
}

const char *PixelMap::colorOrderName() {
  switch (cfg().colorOrder) {
  case LedColorOrder::GRB:
  default:
    return "grb";
  }
}

bool PixelMap::lookupRgb(uint16_t pixelIndex, PixelRgbAddr &out) {
  const PixelMapCfg &m = cfg();
  if (pixelIndex >= m.pixelCount || m.channelsPerPixel < kRgbChannels ||
      m.startChannel == 0 || m.startChannel > kDmxUniverseSize) {
    return false;
  }
  if (m.splitAcrossUniverses) {
    return mapPacked(m, pixelIndex, out);
  }
  return mapWholePixels(m, pixelIndex, out);
}
