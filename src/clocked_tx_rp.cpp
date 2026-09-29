// RP2040 / RP2350 clocked LED output (APA102 family, WS2801, LPD8806, P9813,
// LPD6803): one PIO state machine + DMA channel (RpPioTx), asynchronous, on
// any pin pair. The SD card keeps SPI0. clocked_tx.cpp is the ESP32 version.
#if defined(ARDUINO_ARCH_RP2040)

#include "clocked_tx.h"

#include <cstdlib>

#include "hardware/gpio.h"
#include "log.h"
#include "rp_pio_tx.h"

namespace {

static RpPioTx s_tx;
static uint8_t *s_buf = nullptr;
static int s_data = -1;
static int s_clk = -1;
static uint32_t s_hz = 0;

static void pinLow(uint8_t pin) {
  gpio_init(pin);
  gpio_put(pin, false);
  gpio_set_dir(pin, GPIO_OUT);
}

} // namespace

uint8_t *ClockedTx::buffer(size_t need) {
  if (need > kMaxBytes) {
    return nullptr;
  }
  s_tx.wait();
  if (s_buf == nullptr) {
    s_buf = static_cast<uint8_t *>(malloc(kMaxBytes));
    if (s_buf == nullptr) {
      LOG_C("led", "clocked buffer: no memory");
    }
  }
  return s_buf;
}

void ClockedTx::send(uint8_t data, uint8_t clk, uint32_t hz, size_t len) {
  if (s_buf == nullptr || len == 0 || len > kMaxBytes) {
    return;
  }
  // One state machine serves every clocked output: it moves to the pin pair
  // of the output being sent.
  if (!s_tx.bound() || s_data != data || s_clk != clk || s_hz != hz) {
    s_tx.end();
    s_data = -1;
    s_clk = -1;
    if (!s_tx.beginClocked(data, clk, hz)) {
      LOG_C("led", "clocked bind failed data=%u clk=%u", data, clk);
      return;
    }
    s_data = data;
    s_clk = clk;
    s_hz = hz;
  }
  s_tx.send(s_buf, len);
}

void ClockedTx::wait() { s_tx.wait(); }

void ClockedTx::release(uint8_t data, uint8_t clk) {
  if (s_tx.bound() && s_data == data && s_clk == clk) {
    s_tx.end();
    s_data = -1;
    s_clk = -1;
    return;
  }
  // Not ours (or not bound yet): plain outputs driven low, unless the state
  // machine is using one of them for another output.
  if (s_tx.bound() && (s_data == data || s_clk == clk || s_data == clk ||
                       s_clk == data)) {
    return;
  }
  pinLow(data);
  pinLow(clk);
}

#endif
