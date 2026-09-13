#include "sd_info.h"

#include "board_matrix.h"
#include "live_input.h"
#include "log.h"

#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

static constexpr uint32_t kSdPollMs = 1000;
static constexpr uint32_t kSdListMs = 3000;
static constexpr uint32_t kSdLockForever = 0xFFFFFFFFu;

static bool s_ok = false;
static const char *s_type = "";
static uint32_t s_sizeMb = 0;
static uint32_t s_usedMb = 0;
static uint32_t s_freeMb = 0;
static uint32_t s_lastPoll = 0;
static uint32_t s_lastList = 0;
static bool s_loggedFail = false;
static volatile bool s_exclusiveIo = false;
static SemaphoreHandle_t s_mu = nullptr;

static uint8_t s_nFiles = 0;
static uint8_t s_nDirs = 0;
static char s_files[kSdMaxListFiles][kSdPathLen];
static char s_dirs[kSdMaxListDirs][kSdPathLen];

static char s_pend[kSdMaxListDirs + 1][kSdPathLen];
static uint8_t s_pendDepth[kSdMaxListDirs + 1];

static void ensureMu() {
  if (s_mu == nullptr) {
    s_mu = xSemaphoreCreateMutex();
  }
}

static const char *sdCardTypeName(uint8_t cardType) {
  switch (cardType) {
  case CARD_NONE:
    return "none";
  case CARD_MMC:
    return "MMC";
  case CARD_SD:
    return "SDSC";
  case CARD_SDHC:
    return "SDHC";
  default:
    return "unknown";
  }
}

static char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c + ('a' - 'A'));
  }
  return c;
}

static bool ieq(const char *a, const char *b) {
  while (*a && *b) {
    if (asciiLower(*a++) != asciiLower(*b++)) {
      return false;
    }
  }
  return *a == *b;
}

static int icmp(const char *a, const char *b) {
  while (*a && *b) {
    const unsigned char ca =
        static_cast<unsigned char>(asciiLower(*a++));
    const unsigned char cb =
        static_cast<unsigned char>(asciiLower(*b++));
    if (ca != cb) {
      return static_cast<int>(ca) - static_cast<int>(cb);
    }
  }
  return static_cast<int>(static_cast<unsigned char>(asciiLower(*a))) -
         static_cast<int>(static_cast<unsigned char>(asciiLower(*b)));
}

static int qcmpPath(const void *a, const void *b) {
  return icmp(static_cast<const char *>(a), static_cast<const char *>(b));
}

static const char *baseName(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static bool nameIsDmx(const char *name) {
  const char *b = baseName(name);
  const size_t n = strlen(b);
  if (n < 4) {
    return false;
  }
  const char *e = b + n - 4;
  return e[0] == '.' && asciiLower(e[1]) == 'd' && asciiLower(e[2]) == 'm' &&
         asciiLower(e[3]) == 'x';
}

static bool skipName(const char *name) {
  const char *b = baseName(name);
  if (b[0] == '\0') {
    return true;
  }
  if (b[0] == '.' && (b[1] == '\0' || (b[1] == '.' && b[2] == '\0'))) {
    return true;
  }
  if (b[0] == '.' && b[1] == '_') {
    return true;
  }
  return ieq(b, "System Volume Information");
}

static bool isRootPath(const char *p) {
  return p && p[0] == '/' && p[1] == '\0';
}

static bool joinPath(char *out, size_t n, const char *dir, const char *name) {
  if (!out || n < 3 || !name || !name[0]) {
    return false;
  }
  if (name[0] == '/') {
    snprintf(out, n, "%s", name);
  } else if (!dir || isRootPath(dir)) {
    snprintf(out, n, "/%s", name);
  } else {
    snprintf(out, n, "%s/%s", dir, name);
  }
  return out[0] != '\0';
}

static void sortPaths(char arr[][kSdPathLen], uint8_t n) {
  if (n > 1) {
    qsort(arr, n, kSdPathLen, qcmpPath);
  }
}

static bool addPath(char arr[][kSdPathLen], uint8_t *n, uint8_t max,
                    const char *path, bool *trunc) {
  if (!path || path[0] == '\0') {
    return false;
  }
  if (*n >= max) {
    *trunc = true;
    return false;
  }
  for (uint8_t i = 0; i < *n; ++i) {
    if (ieq(arr[i], path)) {
      return true;
    }
  }
  snprintf(arr[*n], kSdPathLen, "%s", path);
  if (arr[*n][0] == '\0') {
    return false;
  }
  *n += 1;
  return true;
}

// Caller holds s_mu. Uses s_pend as a BFS queue (not re-entrant).
static void walkFill(const char *root, bool recursive, char files[][kSdPathLen],
                     uint8_t maxFiles, uint8_t *nFiles, char dirs[][kSdPathLen],
                     uint8_t maxDirs, uint8_t *nDirs, bool *trunc) {
  *nFiles = 0;
  if (nDirs) {
    *nDirs = 0;
  }
  if (!root || root[0] == '\0') {
    return;
  }

  uint8_t nPend = 0;
  snprintf(s_pend[0], kSdPathLen, "%s", root);
  s_pendDepth[0] = 1;
  nPend = 1;
  uint8_t qi = 0;

  while (qi < nPend) {
    char cur[kSdPathLen];
    snprintf(cur, sizeof(cur), "%s", s_pend[qi]);
    const uint8_t depth = s_pendDepth[qi];
    qi += 1;

    if (depth > kSdMaxDepth) {
      *trunc = true;
      continue;
    }

    File d = SD.open(cur);
    if (!d) {
      continue;
    }
    if (!d.isDirectory()) {
      d.close();
      continue;
    }

    while (true) {
      File entry = d.openNextFile();
      if (!entry) {
        break;
      }
      const char *nm = entry.name();
      if (skipName(nm)) {
        entry.close();
        continue;
      }
      char path[kSdPathLen];
      if (!joinPath(path, sizeof(path), cur, nm)) {
        *trunc = true;
        entry.close();
        continue;
      }
      if (ieq(path, cur)) {
        entry.close();
        continue;
      }
      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        if (dirs && nDirs && !isRootPath(path)) {
          if (!addPath(dirs, nDirs, maxDirs, path, trunc) && *trunc) {
            break;
          }
        }
        if (recursive && nPend < (kSdMaxListDirs + 1)) {
          bool have = false;
          for (uint8_t i = 0; i < nPend; ++i) {
            if (ieq(s_pend[i], path)) {
              have = true;
              break;
            }
          }
          if (!have) {
            snprintf(s_pend[nPend], kSdPathLen, "%s", path);
            s_pendDepth[nPend] = static_cast<uint8_t>(depth + 1);
            nPend = static_cast<uint8_t>(nPend + 1);
          }
        } else if (recursive) {
          *trunc = true;
        }
      } else if (nameIsDmx(path)) {
        if (!addPath(files, nFiles, maxFiles, path, trunc) && *trunc) {
          break;
        }
      }
    }
    d.close();
  }

  sortPaths(files, *nFiles);
  if (dirs && nDirs) {
    sortPaths(dirs, *nDirs);
  }
}

static void clearTree() {
  s_nFiles = 0;
  s_nDirs = 0;
}

static void fillTreeLocked() {
  if (!s_ok) {
    clearTree();
    return;
  }
  bool trunc = false;
  walkFill("/", true, s_files, kSdMaxListFiles, &s_nFiles, s_dirs,
           kSdMaxListDirs, &s_nDirs, &trunc);
  if (trunc) {
    LOG_V("sd", "list truncated");
  }
  LOG_V("sd", "tree files=%u dirs=%u", s_nFiles, s_nDirs);
}

static void listSdRoot() {
  File root = SD.open("/");
  if (!root) {
    LOG_C("sd", "root open failed");
    return;
  }

  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    LOG_V("sd", "%s %s bytes=%u", entry.isDirectory() ? "dir" : "file",
          entry.name(), static_cast<unsigned>(entry.size()));
    entry.close();
    ++count;
    if (count >= 12) {
      LOG_V("sd", "root listing truncated");
      break;
    }
  }
  root.close();
  LOG_V("sd", "root entries=%d", count);
}

static void clearCache() {
  s_ok = false;
  s_type = "";
  s_sizeMb = 0;
  s_usedMb = 0;
  s_freeMb = 0;
  clearTree();
}

// Caller holds s_mu.
static bool tryMount() {
  if (!SD.begin(kSdCs, SPI, kSdSpiHz)) {
    return false;
  }
  const uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    SD.end();
    return false;
  }

  s_type = sdCardTypeName(cardType);
  const uint64_t card = SD.cardSize();
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  s_sizeMb = static_cast<uint32_t>(card / (1024ULL * 1024ULL));
  s_usedMb = static_cast<uint32_t>(used / (1024ULL * 1024ULL));
  const uint64_t cap = total ? total : card;
  s_freeMb = cap > used
                 ? static_cast<uint32_t>((cap - used) / (1024ULL * 1024ULL))
                 : 0;
  s_ok = true;
  s_loggedFail = false;
  LOG_V("sd", "mount OK type=%s size_MB=%u used_MB=%u free_MB=%u", s_type,
        static_cast<unsigned>(s_sizeMb), static_cast<unsigned>(s_usedMb),
        static_cast<unsigned>(s_freeMb));
  listSdRoot();
  fillTreeLocked();
  s_lastList = millis();
  return true;
}

} // namespace

void SdInfo::begin() {
  clearCache();
  ensureMu();
  LOG_V("sd", "spi cs=%u mosi=%u clk=%u miso=%u hz=%u", kSdCs, kSdMosi, kSdClk,
        kSdMiso, static_cast<unsigned>(kSdSpiHz));
  SPI.begin(kSdClk, kSdMiso, kSdMosi, kSdCs);
  if (!lock(kSdLockForever)) {
    LOG_C("sd", "mount failed");
    s_loggedFail = true;
    return;
  }
  const bool mounted = tryMount();
  unlock();
  if (mounted) {
    return;
  }
  LOG_C("sd", "mount failed");
  s_loggedFail = true;
}

void SdInfo::service() {
  if (LiveInput::active()) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastPoll < kSdPollMs) {
    return;
  }
  s_lastPoll = now;

  if (!s_exclusiveIo) {
    if (!lock(200)) {
      return;
    }

    if (s_ok) {
      File root = SD.open("/");
      if (!root) {
        SD.end();
        clearCache();
        unlock();
        LOG_V("sd", "removed");
        return;
      }
      root.close();
    } else {
      SD.end();
      const bool ok = tryMount();
      unlock();
      if (ok) {
        return;
      }
      if (!s_loggedFail) {
        LOG_C("sd", "mount failed");
        s_loggedFail = true;
      }
      return;
    }
    unlock();
  }

  if (!s_ok) {
    return;
  }
  if (now - s_lastList < kSdListMs) {
    return;
  }
  if (!lock(200)) {
    return;
  }
  fillTreeLocked();
  s_lastList = millis();
  unlock();
}

bool SdInfo::ok() { return s_ok; }

const char *SdInfo::type() { return s_type; }

uint32_t SdInfo::sizeMb() { return s_sizeMb; }

uint32_t SdInfo::usedMb() { return s_usedMb; }

uint32_t SdInfo::freeMb() { return s_freeMb; }

bool SdInfo::lock(uint32_t waitMs) {
  ensureMu();
  if (!s_mu) {
    return false;
  }
  const TickType_t t =
      (waitMs == kSdLockForever) ? portMAX_DELAY : pdMS_TO_TICKS(waitMs);
  return xSemaphoreTake(s_mu, t) == pdTRUE;
}

void SdInfo::unlock() {
  if (s_mu) {
    xSemaphoreGive(s_mu);
  }
}

void SdInfo::setExclusiveIo(bool on) { s_exclusiveIo = on; }

void SdInfo::refreshTree() {
  if (!s_ok) {
    clearTree();
    return;
  }
  if (!lock(1000)) {
    return;
  }
  fillTreeLocked();
  s_lastList = millis();
  unlock();
}

uint8_t SdInfo::fileCount() { return s_nFiles; }

const char *SdInfo::fileAt(uint8_t i) {
  return i < s_nFiles ? s_files[i] : "";
}

uint8_t SdInfo::dirCount() { return s_nDirs; }

const char *SdInfo::dirAt(uint8_t i) { return i < s_nDirs ? s_dirs[i] : ""; }

bool SdInfo::collectPlaylist(const char *dir, bool recursive,
                             char out[][kSdPathLen], uint8_t max, uint8_t *n) {
  if (!n) {
    return false;
  }
  *n = 0;
  if (!out || max == 0 || !dir || !s_ok) {
    return false;
  }
  if (!lock(1000)) {
    return false;
  }
  bool trunc = false;
  uint8_t nf = 0;
  walkFill(dir, recursive, out, max, &nf, nullptr, 0, nullptr, &trunc);
  if (trunc) {
    LOG_V("sd", "list truncated");
  }
  unlock();
  *n = nf;
  return nf > 0;
}
