#pragma once

#if defined(ARDUINO_ARCH_RP2040)

#include <stddef.h>
#include <stdint.h>

#include "hardware/pio.h"

// RP2040 / RP2350: one PIO state machine fed by one DMA channel, sending
// bytes MSB first. The timing is made by the PIO hardware, so it does not
// depend on what the CPU is doing. send() returns while the bytes are still
// going out.
//
// Clockless (WS281x family): per bit the line is high for t1, then high (1)
// or low (0) for t2, then low for t3, in 125 ns units (the chip table in
// pixel_map.cpp). The PIO program is assembled for those times.
// Clocked (SPI mode 0): data changes while the clock is low; the chip
// samples on the rising edge.
class RpPioTx {
public:
  bool beginClockless(uint8_t pin, uint8_t t1, uint8_t t2, uint8_t t3);
  bool beginClocked(uint8_t data, uint8_t clk, uint32_t hz);
  // Waits for the frame in flight, frees the state machine and DMA channel
  // and leaves the pins as plain outputs driven low.
  void end();
  bool bound() const { return bound_; }

  // Start sending len bytes. buf must stay unchanged until wait() returns.
  // Waits for the previous frame (and its latch gap) first.
  void send(const uint8_t *buf, size_t len);
  // Block until the last frame is out and latched.
  void wait();

private:
  bool start(uint8_t length, uint8_t sidePin, int outPin, float div);

  PIO pio_ = nullptr;
  uint sm_ = 0;
  uint offset_ = 0;
  int dma_ = -1;
  uint16_t ins_[4] = {0, 0, 0, 0};
  pio_program_t prog_ = {};
  uint8_t sidePin_ = 0;
  int outPin_ = -1;
  uint32_t bitNs_ = 1250;
  uint32_t latchUs_ = 0;
  uint64_t readyAtUs_ = 0;
  bool bound_ = false;
};

#endif
