#pragma once

// USB serial commands for the companion (identify a board on a COM port,
// provision it without Wi-Fi). One request per line:
//
//   whip <cmd> [args]
//
// Reply: exactly one line starting "@whip " followed by JSON. Log lines may
// come before or after it on the same port; the companion ignores them.
//
//   id                 {ok,cmd,tag,board,chip,fam,ver,api,name,short,mac,net}
//   get                id fields + bri, sd{cs,mosi,clk,miso}, btn, ssid,
//                      play{src,path}, map[{out,chip,order,data,clk,count,
//                      uni,sacn,ch,bri,proto}] (the pixel patch)
//   set {json}         any of name, short, bri, btn (-1 = none), sd{cs,mosi,clk,miso},
//                      play{src,path} (startup playlist)
//   wifi {ssid,pass}   saved; used from the next boot (reply reboot:true)
//   pmap <base64>      v1 pixel-map blob (same bytes as NVS pmap/blob)
//   pmap               read it back: {segs,px,blob,map[]}; blob is what pmap
//                      takes, so a patch copies from one board to another
//   boot               RP boards: restart into the UF2 drive
//   test <mode> [out]  LED test pattern on one output: rainbow, cycle, ends, off
//   quiet 0|1          stop / resume log lines on Serial
//   reboot             reply, then restart
class SerialCmd {
public:
  // Call from loop(). Non-blocking.
  static void service();
};
