#include "log.h"

#include "board_profile.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

LogLevel Log::s_level = LogLevel::Verbose;

static constexpr size_t kLogStoreSize = 4096;
static char s_store[kLogStoreSize];
static char s_dump[kLogStoreSize];
static size_t s_used = 0;
static bool s_overflow = false;
static bool s_hostSeen = false;
static SemaphoreHandle_t s_mu = nullptr;
static TaskHandle_t s_task = nullptr;

static void lockStore() {
  if (s_mu) {
    xSemaphoreTake(s_mu, portMAX_DELAY);
  }
}

static void unlockStore() {
  if (s_mu) {
    xSemaphoreGive(s_mu);
  }
}

static void appendLine(const char *line) {
  const size_t n = strlen(line);
  if (n + 1 > kLogStoreSize) {
    s_overflow = true;
    return;
  }
  if (s_used + n + 1 > kLogStoreSize) {
    s_overflow = true;
    return;
  }
  memcpy(s_store + s_used, line, n);
  s_used += n;
  s_store[s_used++] = '\n';
}

static void logServiceTask(void *) {
  for (;;) {
    Log::service();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void Log::begin(uint32_t baud, LogLevel level) {
  s_level = level;
  s_used = 0;
  s_overflow = false;
  s_hostSeen = false;
  if (s_mu == nullptr) {
    s_mu = xSemaphoreCreateMutex();
  }
  Serial.setTxBufferSize(4096);
  Serial.begin(baud);
  if (s_task == nullptr) {
    xTaskCreatePinnedToCore(logServiceTask, "logsvc", 4096, nullptr, 1, &s_task,
                            BoardProfile::serviceCore());
  }
}

void Log::setLevel(LogLevel level) { s_level = level; }

LogLevel Log::level() { return s_level; }

void Log::service() {
  const bool connected = static_cast<bool>(Serial);
  lockStore();
  if (!connected) {
    s_hostSeen = false;
    unlockStore();
    return;
  }
  if (s_hostSeen) {
    unlockStore();
    return;
  }
  const size_t used = s_used;
  const bool overflow = s_overflow;
  if (used > 0) {
    memcpy(s_dump, s_store, used);
  }
  unlockStore();

  if (used > 0) {
    Serial.write(reinterpret_cast<const uint8_t *>(s_dump), used);
  }
  if (overflow) {
    Serial.println("[C][log] buffer overflow, some early lines lost");
  }
  Serial.printf("[V][log] replay bytes=%u\n", static_cast<unsigned>(used));
  Serial.flush();

  lockStore();
  s_hostSeen = true;
  unlockStore();
}

void Log::print(LogLevel msgLevel, const char *tag, const char *fmt, ...) {
  if (s_level == LogLevel::Off) {
    return;
  }
  if (static_cast<uint8_t>(msgLevel) > static_cast<uint8_t>(s_level)) {
    return;
  }

  const char sev = (msgLevel == LogLevel::Critical) ? 'C' : 'V';
  char line[220];
  int prefix = snprintf(line, sizeof(line), "[%c][%s] ", sev,
                        tag != nullptr ? tag : "-");
  if (prefix < 0) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  if (static_cast<size_t>(prefix) < sizeof(line)) {
    vsnprintf(line + prefix, sizeof(line) - static_cast<size_t>(prefix), fmt,
              args);
  }
  va_end(args);
  line[sizeof(line) - 1] = '\0';

  lockStore();
  appendLine(line);
  const bool live = s_hostSeen && static_cast<bool>(Serial);
  unlockStore();

  if (live) {
    Serial.println(line);
  }
}
