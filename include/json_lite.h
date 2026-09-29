#pragma once

#include <stddef.h>

// Flat JSON field readers (no allocation, no escapes): node-to-node replies
// and serial commands. Shared by every board, with or without a network.
namespace NetHttp {

// Value start after "key": in [from, end), or nullptr.
const char *findKey(const char *from, const char *end, const char *key);
long jsonInt(const char *from, const char *end, const char *key, long fallback);
bool jsonStr(const char *from, const char *end, const char *key, char *out, size_t n);
// "true" after "key":
bool jsonBool(const char *from, const char *end, const char *key);

} // namespace NetHttp
