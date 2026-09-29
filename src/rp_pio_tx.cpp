#if defined(ARDUINO_ARCH_RP2040)

#include "rp_pio_tx.h"

#include <Arduino.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/time.h"

namespace {

// One PIO cycle per chip-table unit.
static constexpr uint32_t kUnitNs = 125;
// Low time that latches a clockless strip. 50 us on old WS2812, 280 us on
// current WS2812B / SK6812.
static constexpr uint32_t kLatchUs = 300;
// The TX FIFO (8 bytes, joined) still drains after DMA wrote the last byte.
static constexpr uint32_t kFifoBytes = 8;
// wait() gives up on a frame that never finishes (state machine stopped).
static constexpr uint64_t kStuckUs = 200000;

static void pinLow(int pin) {
  if (pin < 0) {
    return;
  }
  gpio_init(static_cast<uint>(pin));
  gpio_put(static_cast<uint>(pin), false);
  gpio_set_dir(static_cast<uint>(pin), GPIO_OUT);
}

} // namespace

bool RpPioTx::beginClockless(uint8_t pin, uint8_t t1, uint8_t t2, uint8_t t3) {
  end();
  // Delay field is 4 bits with one side-set bit: 1..16 cycles each.
  if (t1 < 1 || t2 < 1 || t3 < 1 || t1 > 16 || t2 > 16 || t3 > 16) {
    return false;
  }
  // bitloop: out x, 1       side 0 [t3 - 1]
  //          jmp !x do_zero side 1 [t1 - 1]
  //          jmp bitloop    side 1 [t2 - 1]
  // do_zero: nop            side 0 [t2 - 1]
  ins_[0] = static_cast<uint16_t>(pio_encode_out(pio_x, 1) |
                                  pio_encode_sideset(1, 0) |
                                  pio_encode_delay(t3 - 1u));
  ins_[1] = static_cast<uint16_t>(pio_encode_jmp_not_x(3) |
                                  pio_encode_sideset(1, 1) |
                                  pio_encode_delay(t1 - 1u));
  ins_[2] = static_cast<uint16_t>(pio_encode_jmp(0) | pio_encode_sideset(1, 1) |
                                  pio_encode_delay(t2 - 1u));
  ins_[3] = static_cast<uint16_t>(pio_encode_nop() | pio_encode_sideset(1, 0) |
                                  pio_encode_delay(t2 - 1u));
  bitNs_ = (static_cast<uint32_t>(t1) + t2 + t3) * kUnitNs;
  latchUs_ = kLatchUs;
  const float div = static_cast<float>(clock_get_hz(clk_sys)) /
                    (1000000000.0f / static_cast<float>(kUnitNs));
  return start(4, pin, -1, div);
}

bool RpPioTx::beginClocked(uint8_t data, uint8_t clk, uint32_t hz) {
  end();
  if (hz == 0 || data == clk) {
    return false;
  }
  //   out pins, 1 side 0
  //   nop         side 1
  ins_[0] = static_cast<uint16_t>(pio_encode_out(pio_pins, 1) |
                                  pio_encode_sideset(1, 0));
  ins_[1] = static_cast<uint16_t>(pio_encode_nop() | pio_encode_sideset(1, 1));
  bitNs_ = 1000000000u / hz;
  latchUs_ = 0;
  float div = static_cast<float>(clock_get_hz(clk_sys)) /
              (2.0f * static_cast<float>(hz));
  if (div < 1.0f) {
    div = 1.0f;
  }
  return start(2, clk, data, div);
}

bool RpPioTx::start(uint8_t length, uint8_t sidePin, int outPin, float div) {
  prog_ = {};
  prog_.instructions = ins_;
  prog_.length = length;
  prog_.origin = -1;

  const uint lo = (outPin >= 0 && static_cast<uint>(outPin) < sidePin)
                      ? static_cast<uint>(outPin)
                      : sidePin;
  const uint hi = (outPin >= 0 && static_cast<uint>(outPin) > sidePin)
                      ? static_cast<uint>(outPin)
                      : sidePin;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &prog_, &pio_, &sm_, &offset_, lo, hi - lo + 1, true)) {
    return false;
  }
  dma_ = dma_claim_unused_channel(false);
  if (dma_ < 0) {
    pio_remove_program_and_unclaim_sm(&prog_, pio_, sm_, offset_);
    return false;
  }

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset_, offset_ + length - 1u);
  sm_config_set_sideset(&c, 1, false, false);
  sm_config_set_sideset_pins(&c, sidePin);
  if (outPin >= 0) {
    sm_config_set_out_pins(&c, static_cast<uint>(outPin), 1);
  }
  // MSB first, refill after every 8 bits. DMA writes bytes; a byte write to
  // the FIFO register is repeated across the 32-bit bus, so the byte is also
  // in the top 8 bits, which is where a left shift takes it from.
  sm_config_set_out_shift(&c, false, true, 8);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, div);

  pio_gpio_init(pio_, sidePin);
  pio_sm_set_consecutive_pindirs(pio_, sm_, sidePin, 1, true);
  if (outPin >= 0) {
    pio_gpio_init(pio_, static_cast<uint>(outPin));
    pio_sm_set_consecutive_pindirs(pio_, sm_, static_cast<uint>(outPin), 1, true);
  }
  pio_sm_init(pio_, sm_, offset_, &c);
  pio_sm_set_enabled(pio_, sm_, true);

  sidePin_ = sidePin;
  outPin_ = outPin;
  readyAtUs_ = 0;
  bound_ = true;
  return true;
}

void RpPioTx::end() {
  if (!bound_) {
    return;
  }
  wait();
  dma_channel_abort(static_cast<uint>(dma_));
  pio_sm_set_enabled(pio_, sm_, false);
  pio_sm_clear_fifos(pio_, sm_);
  pio_remove_program_and_unclaim_sm(&prog_, pio_, sm_, offset_);
  dma_channel_unclaim(static_cast<uint>(dma_));
  pinLow(sidePin_);
  pinLow(outPin_);
  dma_ = -1;
  outPin_ = -1;
  bound_ = false;
}

void RpPioTx::wait() {
  if (!bound_) {
    return;
  }
  const uint chan = static_cast<uint>(dma_);
  const uint64_t giveUp = time_us_64() + kStuckUs;
  bool overran = false;
  while (dma_channel_is_busy(chan) || !pio_sm_is_tx_fifo_empty(pio_, sm_)) {
    const uint64_t now = time_us_64();
    if (now >= giveUp) {
      dma_channel_abort(chan);
      pio_sm_clear_fifos(pio_, sm_);
      break;
    }
    overran = now >= readyAtUs_;
    if (readyAtUs_ > now && readyAtUs_ - now > 2000) {
      delay(1);
    }
  }
  // Took longer than reckoned: allow the byte still shifting, then the latch.
  uint64_t until = readyAtUs_;
  if (overran) {
    until = time_us_64() + (8u * bitNs_) / 1000u + 1u + latchUs_;
  }
  for (uint64_t now = time_us_64(); now < until; now = time_us_64()) {
    if (until - now > 2000) {
      delay(1);
    }
  }
}

void RpPioTx::send(const uint8_t *buf, size_t len) {
  if (!bound_ || buf == nullptr || len == 0) {
    return;
  }
  wait();
  const uint chan = static_cast<uint>(dma_);
  dma_channel_config cfg = dma_channel_get_default_config(chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_dreq(&cfg, pio_get_dreq(pio_, sm_, true));
  const uint64_t sendUs =
      (static_cast<uint64_t>(len + kFifoBytes) * 8u * bitNs_) / 1000u;
  readyAtUs_ = time_us_64() + sendUs + latchUs_;
  dma_channel_configure(chan, &cfg, &pio_->txf[sm_], buf, len, true);
}

#endif
