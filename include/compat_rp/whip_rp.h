#pragma once

// Forced into every RP2040 / RP2350 translation unit ([rp] build_flags
// -include): ESP32 core helpers the shared code calls. The RP boards have no
// PSRAM.
#ifdef __cplusplus
#include <stdlib.h>

static inline bool psramFound() { return false; }
static inline void *ps_malloc(size_t n) { return malloc(n); }
#endif
