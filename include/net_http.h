#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <stddef.h>

// Minimal node-to-node HTTP client and JSON field readers (Distribute,
// Stream, OTA peer updates). Blocking; call from a worker task.
namespace NetHttp {

static constexpr uint32_t kTimeoutMs = 8000;

// GET path into buf (NUL-terminated, headers included). True on HTTP 200.
bool get(const IPAddress &ip, const char *path, char *buf, size_t cap, size_t &len);
// Status code of the reply on c (reads the status line only).
int readStatusCode(WiFiClient &c, uint32_t timeoutMs = kTimeoutMs * 4);
// POST an urlencoded body. True on HTTP 200.
bool postForm(const IPAddress &ip, const char *path, const String &body);
String urlEncode(const char *s);

// Value start after "key": in [from, end), or nullptr.
const char *findKey(const char *from, const char *end, const char *key);
long jsonInt(const char *from, const char *end, const char *key, long fallback);
bool jsonStr(const char *from, const char *end, const char *key, char *out, size_t n);
// "true" after "key":
bool jsonBool(const char *from, const char *end, const char *key);

} // namespace NetHttp
