#pragma once

#include <Arduino.h>
#include <stdint.h>

// #region agent log
static inline void dbg981(const char *hyp, const char *loc, const char *msg,
                          uint32_t a = 0, uint32_t b = 0) {
  Serial.printf("[dbg981] hyp=%s loc=%s msg=%s a=%u b=%u ms=%u\n", hyp, loc,
                msg, static_cast<unsigned>(a), static_cast<unsigned>(b),
                static_cast<unsigned>(millis()));
  Serial.flush();
}
// #endregion
