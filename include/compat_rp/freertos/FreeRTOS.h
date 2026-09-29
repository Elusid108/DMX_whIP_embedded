#pragma once

// RP2040 / RP2350 (arduino-pico, FreeRTOS SMP): the ESP-IDF include path and
// the one ESP-only task call the shared code uses.
#include <FreeRTOS.h>
#include <task.h>

// ESP-IDF counts task stacks in bytes and pins to one core; FreeRTOS SMP
// counts words and takes a core mask.
#ifndef xTaskCreatePinnedToCore
static inline BaseType_t whipTaskPinned(TaskFunction_t fn, const char *name,
                                        uint32_t stackBytes, void *arg,
                                        UBaseType_t prio, TaskHandle_t *out,
                                        BaseType_t core) {
  return xTaskCreateAffinitySet(fn, name,
                                static_cast<configSTACK_DEPTH_TYPE>(stackBytes / sizeof(StackType_t)),
                                arg, prio,
                                static_cast<UBaseType_t>(1u << (core & 1)), out);
}
#define xTaskCreatePinnedToCore whipTaskPinned
#endif
