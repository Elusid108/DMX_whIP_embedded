#pragma once

// SoftAP config portal (product defaults, not board pins).
static constexpr char kApSsid[] = "dmxwhip";
static constexpr char kApPass[] = "pass1234";

class WifiSetup {
public:
  static void begin();
  static void service();
};
