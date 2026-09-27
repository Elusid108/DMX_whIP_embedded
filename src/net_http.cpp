#include "net_http.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

bool get(const IPAddress &ip, const char *path, char *buf, size_t cap, size_t &len) {
  WiFiClient c;
  c.setTimeout(kTimeoutMs / 1000);
  len = 0;
  if (cap == 0) {
    return false;
  }
  buf[0] = '\0';
  if (!c.connect(ip, 80, kTimeoutMs)) {
    return false;
  }
  c.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
           ip.toString().c_str());
  const uint32_t t0 = millis();
  while ((c.connected() || c.available()) && millis() - t0 < kTimeoutMs) {
    const int a = c.available();
    if (a <= 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    const size_t room = cap - 1 - len;
    if (room == 0) {
      break;
    }
    len += c.read(reinterpret_cast<uint8_t *>(buf + len),
                  static_cast<size_t>(a) < room ? a : room);
  }
  buf[len] = '\0';
  c.stop();
  return strncmp(buf, "HTTP/1.1 200", 12) == 0 || strncmp(buf, "HTTP/1.0 200", 12) == 0;
}

int readStatusCode(WiFiClient &c, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  char line[40];
  size_t i = 0;
  while (millis() - t0 < timeoutMs) {
    if (!c.available()) {
      if (!c.connected()) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    const int ch = c.read();
    if (ch == '\n' || i + 1 >= sizeof(line)) {
      break;
    }
    line[i++] = static_cast<char>(ch);
  }
  line[i] = '\0';
  const char *sp = strchr(line, ' ');
  return sp ? atoi(sp + 1) : 0;
}

bool postForm(const IPAddress &ip, const char *path, const String &body) {
  WiFiClient c;
  if (!c.connect(ip, 80, kTimeoutMs)) {
    return false;
  }
  c.printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: "
           "application/x-www-form-urlencoded\r\nContent-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           path, ip.toString().c_str(), static_cast<unsigned>(body.length()));
  c.print(body);
  const int code = readStatusCode(c);
  c.stop();
  return code == 200;
}

String urlEncode(const char *s) {
  String out;
  static const char hex[] = "0123456789ABCDEF";
  for (; *s; ++s) {
    const uint8_t ch = static_cast<uint8_t>(*s);
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += hex[ch >> 4];
      out += hex[ch & 15];
    }
  }
  return out;
}

} // namespace NetHttp
