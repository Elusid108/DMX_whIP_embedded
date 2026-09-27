#include "clocked_tx.h"

#include "log.h"

#include <Arduino.h>
#include <cstring>

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc_caps.h"

#if SOC_SPI_PERIPH_NUM > 2
#include "driver/spi_master.h"
#include "soc/spi_periph.h"
#define CLOCKED_TX_SPI 1
#else
#define CLOCKED_TX_SPI 0
#endif

namespace {

static constexpr uint8_t kNoPin = 0xFF;

static uint8_t *s_buf[2] = {nullptr, nullptr};
static uint8_t s_next = 0;
static bool s_allocFailed = false;

static void pinLow(uint8_t pin) {
  esp_rom_gpio_pad_select_gpio(pin);
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
}

#if CLOCKED_TX_SPI

// Each distinct clock rate is its own device on the bus.
static constexpr uint8_t kMaxDevs = 3;
static constexpr spi_host_device_t kHost = SPI3_HOST;

struct Dev {
  uint32_t hz;
  spi_device_handle_t h;
};

static bool s_busUp = false;
static bool s_busFailed = false;
static Dev s_devs[kMaxDevs];
static uint8_t s_nDevs = 0;
static spi_transaction_t s_trans;
static spi_device_handle_t s_inFlight = nullptr;
static int8_t s_inFlightBuf = -1;
static uint8_t s_routedData = kNoPin;
static uint8_t s_routedClk = kNoPin;

static bool busUp() {
  if (s_busUp) {
    return true;
  }
  if (s_busFailed) {
    return false;
  }
  spi_bus_config_t bus = {};
  // No pins at init: route() connects them per output.
  bus.mosi_io_num = -1;
  bus.miso_io_num = -1;
  bus.sclk_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = static_cast<int>(ClockedTx::kMaxBytes);
  bus.flags = SPICOMMON_BUSFLAG_MASTER;
  const esp_err_t err = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    s_busFailed = true;
    LOG_C("led", "clocked spi init failed err=%d (bit-bang)",
          static_cast<int>(err));
    return false;
  }
  s_busUp = true;
  LOG_V("led", "clocked spi3 dma");
  return true;
}

static spi_device_handle_t devFor(uint32_t hz) {
  for (uint8_t i = 0; i < s_nDevs; ++i) {
    if (s_devs[i].hz == hz) {
      return s_devs[i].h;
    }
  }
  if (s_nDevs >= kMaxDevs) {
    return nullptr;
  }
  spi_device_interface_config_t cfg = {};
  cfg.mode = 0;
  cfg.clock_speed_hz = static_cast<int>(hz);
  cfg.spics_io_num = -1;
  cfg.queue_size = 1;
  spi_device_handle_t h = nullptr;
  if (spi_bus_add_device(kHost, &cfg, &h) != ESP_OK) {
    return nullptr;
  }
  s_devs[s_nDevs++] = {hz, h};
  return h;
}

static void waitSpi() {
  if (s_inFlight == nullptr) {
    return;
  }
  spi_transaction_t *done = nullptr;
  spi_device_get_trans_result(s_inFlight, &done, portMAX_DELAY);
  s_inFlight = nullptr;
  s_inFlightBuf = -1;
}

static void unroute() {
  if (s_routedData != kNoPin) {
    pinLow(s_routedData);
  }
  if (s_routedClk != kNoPin) {
    pinLow(s_routedClk);
  }
  s_routedData = kNoPin;
  s_routedClk = kNoPin;
}

static void route(uint8_t data, uint8_t clk) {
  if (data == s_routedData && clk == s_routedClk) {
    return;
  }
  unroute();
  const spi_signal_conn_t &sig = spi_periph_signal[kHost];
  esp_rom_gpio_pad_select_gpio(data);
  esp_rom_gpio_pad_select_gpio(clk);
  gpio_set_direction(static_cast<gpio_num_t>(data), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(clk), GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(data, sig.spid_out, false, false);
  esp_rom_gpio_connect_out_signal(clk, sig.spiclk_out, false, false);
  s_routedData = data;
  s_routedClk = clk;
}

#endif // CLOCKED_TX_SPI

static uint32_t s_cpuMhz = 0;

static inline void spinUntil(uint32_t t) {
  while (static_cast<int32_t>(esp_cpu_get_cycle_count() - t) < 0) {
  }
}

// Paced bit-bang: one register write per edge, half period from the CPU
// cycle counter. An interrupt can stretch a bit; these chips are clocked,
// so a late edge is harmless.
static void bitBang(uint8_t data, uint8_t clk, uint32_t hz, const uint8_t *b,
                    size_t n) {
  if (s_cpuMhz == 0) {
    s_cpuMhz = getCpuFrequencyMhz();
  }
  gpio_dev_t *hw = GPIO_LL_GET_HW(GPIO_PORT_0);
  uint32_t half = (s_cpuMhz * 1000000u) / (2u * hz);
  if (half == 0) {
    half = 1;
  }
  gpio_ll_set_level(hw, clk, 0);
  uint32_t t = esp_cpu_get_cycle_count();
  for (size_t i = 0; i < n; ++i) {
    const uint8_t v = b[i];
    for (int bit = 7; bit >= 0; --bit) {
      gpio_ll_set_level(hw, data, (v >> bit) & 1u);
      t += half;
      spinUntil(t);
      gpio_ll_set_level(hw, clk, 1);
      t += half;
      spinUntil(t);
      gpio_ll_set_level(hw, clk, 0);
    }
  }
}

static bool ensureBuffers() {
  if (s_buf[0] != nullptr) {
    return true;
  }
  if (s_allocFailed) {
    return false;
  }
#if CLOCKED_TX_SPI
  const uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
  s_buf[0] =
      static_cast<uint8_t *>(heap_caps_malloc(ClockedTx::kMaxBytes, caps));
  s_buf[1] =
      static_cast<uint8_t *>(heap_caps_malloc(ClockedTx::kMaxBytes, caps));
#else
  s_buf[0] = static_cast<uint8_t *>(malloc(ClockedTx::kMaxBytes));
  s_buf[1] = s_buf[0];
#endif
  if (s_buf[0] == nullptr || s_buf[1] == nullptr) {
    if (s_buf[1] != s_buf[0]) {
      free(s_buf[1]);
    }
    free(s_buf[0]);
    s_buf[0] = nullptr;
    s_buf[1] = nullptr;
    s_allocFailed = true;
    LOG_C("led", "clocked buffer alloc failed");
    return false;
  }
  return true;
}

} // namespace

uint8_t *ClockedTx::buffer(size_t need) {
  if (need > kMaxBytes || !ensureBuffers()) {
    return nullptr;
  }
#if CLOCKED_TX_SPI
  // Double buffer: encode one output while the previous one is on the wire.
  if (s_inFlightBuf == static_cast<int8_t>(s_next)) {
    waitSpi();
  }
#endif
  return s_buf[s_next];
}

void ClockedTx::send(uint8_t data, uint8_t clk, uint32_t hz, size_t len) {
  if (len == 0 || s_buf[0] == nullptr) {
    return;
  }
  const uint8_t which = s_next;
#if CLOCKED_TX_SPI
  s_next = static_cast<uint8_t>(s_next ^ 1u);
  if (busUp()) {
    spi_device_handle_t dev = devFor(hz);
    if (dev != nullptr) {
      waitSpi();
      route(data, clk);
      memset(&s_trans, 0, sizeof(s_trans));
      s_trans.length = len * 8;
      s_trans.tx_buffer = s_buf[which];
      if (spi_device_queue_trans(dev, &s_trans, portMAX_DELAY) == ESP_OK) {
        s_inFlight = dev;
        s_inFlightBuf = static_cast<int8_t>(which);
        return;
      }
    }
  }
  waitSpi();
  unroute();
#endif
  bitBang(data, clk, hz, s_buf[which], len);
}

void ClockedTx::wait() {
#if CLOCKED_TX_SPI
  waitSpi();
#endif
}

void ClockedTx::release(uint8_t data, uint8_t clk) {
#if CLOCKED_TX_SPI
  waitSpi();
  if (data == s_routedData || clk == s_routedClk || data == s_routedClk ||
      clk == s_routedData) {
    unroute();
  }
#endif
  pinLow(data);
  pinLow(clk);
}
