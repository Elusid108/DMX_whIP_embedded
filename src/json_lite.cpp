#include "json_lite.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace NetHttp {

const char *findKey(const char *from, const char *end, const char *key) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const size_t n = strlen(needle);
  for (const char *p = from; p && p + n <= end; ++p) {
    p = static_cast<const char *>(memchr(p, '"', static_cast<size_t>(end - p)));
    if (!p || p + n > end) {
      return nullptr;
    }
    if (memcmp(p, needle, n) == 0) {
      const char *c = p + n;
      while (c < end && (*c == ' ' || *c == ':')) {
        ++c;
      }
      return c;
    }
  }
  return nullptr;
}

long jsonInt(const char *from, const char *end, const char *key, long fallback) {
  const char *v = findKey(from, end, key);
  return v ? strtol(v, nullptr, 10) : fallback;
}

bool jsonStr(const char *from, const char *end, const char *key, char *out, size_t n) {
  const char *v = findKey(from, end, key);
  if (!v || *v != '"') {
    return false;
  }
  ++v;
  size_t i = 0;
  while (v < end && *v != '"' && i + 1 < n) {
    out[i++] = *v++;
  }
  out[i] = '\0';
  return true;
}

bool jsonBool(const char *from, const char *end, const char *key) {
  const char *v = findKey(from, end, key);
  return v && end - v >= 4 && memcmp(v, "true", 4) == 0;
}

} // namespace NetHttp
