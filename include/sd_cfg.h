#pragma once

// whip.cfg in the SD card's root: settings that travel with the card, for
// boards with no network (RP2040 / RP2350 standalone player). Read each time
// a card is mounted. What it sets lasts until the board restarts and is not
// saved, so taking the file away brings the saved settings back.
//
// One "key=value" per line; "#" starts a comment; unknown keys are logged.
//
//   name=Stage-01        node name (short name: its first 17 characters)
//   bri=32               brightness 0-255
//   uni=3                Art-Net start universe (sACN is one higher)
//   ch=1                 start channel 1-512
//   count=64             pixels on the first output
//   order=grb            colour order
//   chip=ws2812b         LED chip
//   data=26              LED data GPIO
//   clk=27               LED clock GPIO (clocked chips)
//   play=root            what to play: root, none, file:/look.dmx,
//                        folder:/shows
//   loop=all             a file source: all (the list) or one (that file)
//
// The LED keys change the first segment of the patch only.
class SdCfg {
public:
  // Call from loop(). Does nothing on boards with a network.
  static void service();
};
