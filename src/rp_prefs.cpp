// RP2040 / RP2350 Preferences on LittleFS (include/compat_rp/Preferences.h).
#if defined(ARDUINO_ARCH_RP2040)

#include <Preferences.h>

#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace {

static bool s_mounted = false;

static bool mount() {
  if (!s_mounted) {
    // Formats the (empty) filesystem area on first boot.
    s_mounted = LittleFS.begin();
  }
  return s_mounted;
}

} // namespace

bool Preferences::begin(const char *ns, bool readOnly) {
  if (ns == nullptr || !*ns || strlen(ns) >= sizeof(ns_) || !mount()) {
    return false;
  }
  strcpy(ns_, ns);
  readOnly_ = readOnly;
  open_ = true;
  if (!readOnly) {
    LittleFS.mkdir("/kv");
    char dir[24];
    snprintf(dir, sizeof(dir), "/kv/%s", ns_);
    LittleFS.mkdir(dir);
  }
  return true;
}

void Preferences::end() { open_ = false; }

bool Preferences::path(const char *key, char *out, size_t n) const {
  if (!open_ || key == nullptr || !*key || strlen(key) > 15) {
    return false;
  }
  snprintf(out, n, "/kv/%s/%s", ns_, key);
  return true;
}

size_t Preferences::readRaw(const char *key, void *buf, size_t maxLen, bool &found) {
  found = false;
  char p[40];
  if (!path(key, p, sizeof(p))) {
    return 0;
  }
  File f = LittleFS.open(p, "r");
  if (!f) {
    return 0;
  }
  found = true;
  const size_t n = buf ? f.read(static_cast<uint8_t *>(buf), maxLen) : f.size();
  f.close();
  return n;
}

size_t Preferences::writeRaw(const char *key, const void *value, size_t len) {
  char p[40];
  if (readOnly_ || !path(key, p, sizeof(p))) {
    return 0;
  }
  File f = LittleFS.open(p, "w");
  if (!f) {
    return 0;
  }
  const size_t n = f.write(static_cast<const uint8_t *>(value), len);
  f.close();
  return n;
}

uint8_t Preferences::getUChar(const char *key, uint8_t fallback) {
  uint8_t v = 0;
  bool found = false;
  return readRaw(key, &v, 1, found) == 1 ? v : fallback;
}

size_t Preferences::putUChar(const char *key, uint8_t value) {
  return writeRaw(key, &value, 1);
}

uint16_t Preferences::getUShort(const char *key, uint16_t fallback) {
  uint16_t v = 0;
  bool found = false;
  return readRaw(key, &v, 2, found) == 2 ? v : fallback;
}

size_t Preferences::putUShort(const char *key, uint16_t value) {
  return writeRaw(key, &value, 2);
}

String Preferences::getString(const char *key, const String &fallback) {
  char buf[256];
  bool found = false;
  const size_t n = readRaw(key, buf, sizeof(buf) - 1, found);
  if (!found) {
    return fallback;
  }
  buf[n] = '\0';
  return String(buf);
}

size_t Preferences::getString(const char *key, char *buf, size_t maxLen) {
  if (buf == nullptr || maxLen == 0) {
    return 0;
  }
  bool found = false;
  const size_t n = readRaw(key, buf, maxLen - 1, found);
  buf[found ? n : 0] = '\0';
  return found ? n : 0;
}

size_t Preferences::putString(const char *key, const char *value) {
  const char *v = value ? value : "";
  return writeRaw(key, v, strlen(v));
}

size_t Preferences::getBytesLength(const char *key) {
  bool found = false;
  return readRaw(key, nullptr, 0, found);
}

size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen) {
  bool found = false;
  return buf ? readRaw(key, buf, maxLen, found) : 0;
}

size_t Preferences::putBytes(const char *key, const void *value, size_t len) {
  return value ? writeRaw(key, value, len) : 0;
}

#endif
