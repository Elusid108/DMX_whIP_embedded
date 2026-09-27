#pragma once

#include <IPAddress.h>
#include <stdint.h>

// Self-hosted show network (NVS namespace "show").
//
// Host: this node runs the show SSID as its SoftAP on 10.77.0.1/24 (built-in
// DHCP, up to 10 clients, DTIM 1 so broadcasts are not held), never hides
// it, and is the cue-bus clock master (role 3). It replaces the dmxwhip
// setup AP; the portal is at http://10.77.0.1.
// Member: joins the show SSID first, keeps retrying, and falls back to the
// saved venue network in between.
// Standalone: the setup AP / saved network as before.
//
// Changing the role reboots (POST /shownet).

enum class ShowRole : uint8_t { Standalone = 0, Host = 1, Member = 2 };

static const IPAddress kShowHostIp(10, 77, 0, 1);
static const IPAddress kShowMask(255, 255, 255, 0);

class ShowNet {
public:
  static void begin();
  static ShowRole role();
  static const char *roleName();
  static const char *ssid();
  static const char *pass();
  static uint8_t channel();
  // Role is Host or Member and an SSID is set.
  static bool active();
  static bool isHost();
  static bool isMember();
  static bool parseRole(const char *s, ShowRole &out);
  static bool save(ShowRole role, const char *ssid, const char *pass,
                   uint8_t channel);
};
