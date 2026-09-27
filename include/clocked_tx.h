#pragma once

#include <stddef.h>
#include <stdint.h>

// Byte-stream transmit for clocked (data + clock) LED chips: APA102 family,
// WS2801, LPD8806, P9813, LPD6803. Every one of their frames is byte-aligned,
// so LedBus encodes an output into buffer() and hands it to send().
//
// SPI mode 0, MSB first (data changes while the clock is low; the chip
// samples on the rising edge): the same bits the old digitalWrite path sent.
//
// ESP32-S3: SPI3 with DMA, asynchronous. The SD card owns SPI2. Pins are
// routed through the GPIO matrix per output, so every clocked output shares
// the one host, and send() returns while the bytes are still going out.
// Chips with a single general-purpose SPI host (C5, owned by the SD card):
// paced bit-bang with direct GPIO register writes, blocking.
//
// Buffers are allocated on first use, so a node with no clocked output pays
// nothing.

class ClockedTx {
public:
  // Encode target for the next send(). nullptr if need exceeds kMaxBytes or
  // memory is short. Waits for the transfer that last used this buffer.
  static uint8_t *buffer(size_t need);

  // Transmit len bytes of the last buffer() on data/clk at hz.
  static void send(uint8_t data, uint8_t clk, uint32_t hz, size_t len);

  // Block until nothing is in flight (before touching pins or buffers).
  static void wait();

  // Return a pin pair to plain GPIO driven low (map change / RMT rebind).
  static void release(uint8_t data, uint8_t clk);

  static constexpr size_t kMaxBytes = 5 * 1024 + 128;
};
