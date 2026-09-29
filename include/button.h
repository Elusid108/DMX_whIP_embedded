#pragma once

// Play / pause push button on BoardProfile::buttonPin() (pin to GND, internal
// pull-up, pressed = low). One action per press, on the press edge after a
// 30 ms debounce. Inside a group cue it pauses / resumes the whole group, as
// the portal's Pause does; otherwise this node only. Ignored while live input
// owns the LEDs or no show is loaded.
class Button {
public:
  // Also call after the pin changes.
  static void begin();
  static void service();
};
