#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// RP2040 / RP2350: the ESP32 Preferences (NVS) API the shared code uses, kept
// as one small file per key on LittleFS: /kv/<namespace>/<key>. Only the
// calls the firmware makes are here (see src/rp_prefs.cpp).
class Preferences {
public:
  bool begin(const char *ns, bool readOnly = false);
  void end();

  uint8_t getUChar(const char *key, uint8_t fallback = 0);
  size_t putUChar(const char *key, uint8_t value);
  uint16_t getUShort(const char *key, uint16_t fallback = 0);
  size_t putUShort(const char *key, uint16_t value);
  String getString(const char *key, const String &fallback = String());
  // Into buf (NUL-terminated); bytes copied, 0 when missing.
  size_t getString(const char *key, char *buf, size_t maxLen);
  size_t putString(const char *key, const char *value);
  size_t putString(const char *key, const String &value) { return putString(key, value.c_str()); }
  size_t getBytesLength(const char *key);
  size_t getBytes(const char *key, void *buf, size_t maxLen);
  size_t putBytes(const char *key, const void *value, size_t len);

private:
  bool path(const char *key, char *out, size_t n) const;
  size_t readRaw(const char *key, void *buf, size_t maxLen, bool &found);
  size_t writeRaw(const char *key, const void *value, size_t len);

  char ns_[16] = {0};
  bool open_ = false;
  bool readOnly_ = true;
};
