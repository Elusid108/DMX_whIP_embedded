#pragma once

#include <IPAddress.h>

// SoftAP config portal (product defaults, not board pins).
static constexpr char kApSsid[] = "dmxwhip";
static constexpr char kApPass[] = "pass1234";
static const IPAddress kApIp(4, 3, 2, 1);
static const IPAddress kApMask(255, 255, 255, 0);

class WifiSetup {
public:
  static void begin();
  static void service();
  // STA credentials to NVS (the serial wifi command). Used from the next boot.
  static void saveCredentials(const char *ssid, const char *pass);
  static const char *savedSsid();
};
