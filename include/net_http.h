#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <stddef.h>

#include "json_lite.h"

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

} // namespace NetHttp
