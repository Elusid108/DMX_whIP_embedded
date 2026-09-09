#pragma once

#include <stdint.h>

enum class LogLevel : uint8_t {
  Off = 0,
  Critical = 1,
  Verbose = 2,
};

class Log {
public:
  static void begin(uint32_t baud, LogLevel level);
  static void setLevel(LogLevel level);
  static LogLevel level();
  static void print(LogLevel msgLevel, const char *tag, const char *fmt, ...);
  static void service();

private:
  static LogLevel s_level;
};

#define LOG_C(tag, fmt, ...)                                                   \
  Log::print(LogLevel::Critical, tag, fmt, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...)                                                   \
  Log::print(LogLevel::Verbose, tag, fmt, ##__VA_ARGS__)
