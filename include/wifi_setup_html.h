#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>dmxwhip v0.20.1</title>
<style>
:root{--bg:#09090b;--chrome:#18181b;--border:#27272a;--text:#e4e4e7;--muted:#71717a;--accent:#22d3ee}
html,body{height:100%;height:100dvh;margin:0;overflow:hidden}
body{display:flex;flex-direction:column;box-sizing:border-box;padding:10px 12px;font-family:system-ui,sans-serif;background:var(--bg);color:var(--text);max-width:28rem;margin:0 auto}
.strip{flex:0 0 auto;padding:0 0 8px;margin:0 0 8px;border-bottom:1px solid var(--border);text-align:center}
#nameView{margin:0;font-size:1.35rem;font-weight:700;cursor:pointer;word-break:break-word}
#name{display:none;text-align:center;font-weight:700}
body.editing #name{display:block}
body.editing #nameView{display:none}
#identify,#reboot{width:auto;min-width:7rem;margin:0}
.livehead{display:flex;justify-content:center;margin:0 0 8px}
.mode{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);border:1px solid var(--border);background:var(--chrome)}
.mode i{width:6px;height:6px;border-radius:50%;background:currentColor;opacity:.4}
.mode.live{color:var(--accent);border-color:#22d3ee66}
.mode.play{color:#86efac;border-color:#86efac66}
.mode.live i,.mode.play i{opacity:1}
.dash{flex:1 1 auto;min-height:0;display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr;gap:8px}
.tile{min-height:0;display:flex;flex-direction:column;background:var(--chrome);border:1px solid var(--border);border-radius:12px;padding:8px 10px;overflow:hidden}
.clab{font-size:10px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 4px}
.tval{font-weight:700;font-size:.95rem;line-height:1.2;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.tmono{font-family:ui-monospace,monospace;font-variant-numeric:tabular-nums;font-size:11px;color:var(--muted);margin:2px 0 0}
.sig{display:flex;align-items:flex-end;gap:8px;margin:auto 0 6px}
.bars{display:flex;align-items:flex-end;gap:2px;height:14px}
.bars i{width:3px;border-radius:1px;background:var(--border)}
.bars i:nth-child(1){height:28%}
.bars i:nth-child(2){height:50%}
.bars i:nth-child(3){height:72%}
.bars i:nth-child(4){height:100%}
.bars[data-n="1"] i:nth-child(-n+1),.bars[data-n="2"] i:nth-child(-n+2),.bars[data-n="3"] i:nth-child(-n+3),.bars[data-n="4"] i:nth-child(-n+4){background:var(--fill,var(--accent))}
.bars.mid{--fill:#f59e0b}
.bars.lo{--fill:#f87171}
.trio{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;margin:auto 0 4px;text-align:center}
.trio b{display:block;font:600 1.25rem/1.1 ui-monospace,monospace;font-variant-numeric:tabular-nums}
.trio em{display:block;font-style:normal;font-size:9px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.cap{margin:0;font-size:10px;color:var(--muted);line-height:1.3;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.cap.stale{color:#f59e0b}
.sbar{height:6px;border-radius:99px;background:#27272a;overflow:hidden;margin:auto 0 8px}
.sbar i{display:block;height:100%;width:0;background:var(--accent);border-radius:99px}
.pips{display:flex;gap:10px;margin-top:auto}
.pip{display:inline-flex;align-items:center;gap:4px;font-size:10px;letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
.pip i{width:6px;height:6px;border-radius:50%;background:var(--border)}
.pip.on{color:#86efac}
.pip.on i{background:#86efac}
.pip.warn{color:#f59e0b}
.pip.warn i{background:#f59e0b}
.health{flex:0 0 auto;margin:8px 0 0;text-align:center;font:11px/1.35 ui-monospace,monospace;color:var(--muted)}
.health p{margin:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.health .hi{color:var(--text)}
.readout{font-family:ui-monospace,monospace;font-variant-numeric:tabular-nums;font-size:11px;color:var(--muted);margin:6px 0 0;line-height:1.35;word-break:break-word}
#liveLock{display:none;flex:0 0 auto;margin:0 0 8px;padding:14px 12px;background:#7f1d1d;color:#fecaca;font-weight:800;font-size:1.05rem;line-height:1.3;text-align:center;border-radius:8px}
#liveLock.on{display:block}
.patchbar{justify-content:space-between;align-items:center}
.patchbar #mapAdd{margin-left:auto}
.patchops button.icon{padding:4px 7px;font-size:1rem;line-height:1}
.tabs{display:flex;gap:0;flex:0 0 auto;border-bottom:1px solid var(--border);margin:0 0 8px}
.tabs button{flex:1;margin:0;padding:8px 4px;border:0;border-bottom:2px solid transparent;border-radius:0;background:transparent;color:var(--muted);font-weight:500;font-size:.8rem}
.tabs button.on{color:var(--accent);border-bottom-color:var(--accent)}
#viewLive,#viewPlay,#viewPixels,#viewSetup{flex:1 1 auto;min-height:0;display:none;flex-direction:column}
#viewLive.on,#viewPlay.on,#viewPixels.on,#viewSetup.on{display:flex}
#viewPixels{overflow-y:auto;-webkit-overflow-scrolling:touch}
#viewLive{overflow:hidden}
.lab{display:block;margin:8px 0 2px;font-size:10px;font-weight:500;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.mod{margin-top:10px;padding-top:10px;border-top:1px solid var(--border)}
#list,#plist{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;border:1px solid var(--border);border-radius:6px;margin:6px 0;padding:3px;background:var(--bg)}
.net,.playrow{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;margin:3px;background:var(--chrome);border:1px solid var(--border);border-radius:6px;cursor:pointer}
.net.sel,.playrow.sel{border-color:#22d3ee66;box-shadow:inset 2px 0 0 var(--accent)}
.row{display:flex;flex-wrap:wrap;gap:6px;flex:0 0 auto}
.row button{width:auto;margin:0;flex:0 0 auto}
input,button,select{width:100%;box-sizing:border-box;padding:8px 10px;font-size:1rem;border-radius:6px;border:1px solid var(--border);background:var(--chrome);color:var(--text)}
.brirow{display:flex;gap:8px;align-items:center}
.brirow input[type=range]{flex:1 1 auto;padding:8px 0;min-width:0;accent-color:var(--accent)}
#brinum{width:4.6rem;flex:0 0 4.6rem;padding:8px 6px;text-align:right;font-family:ui-monospace,monospace}
button{background:var(--chrome);border:1px solid var(--border);margin:6px 0 0;font-weight:600;color:var(--text)}
button.pri{background:var(--accent);border-color:var(--accent);color:var(--bg)}
#savedrow,#renrow,#fileopts,#folderopts,#foldernrow,#note{display:none}
#savedrow.on,#renrow.on,#fileopts.on,#folderopts.on,#foldernrow.on,#note.on{display:block}
.clkrow,.briwarn,.cntwarn{display:none}
.clkrow.on,.briwarn.on,.cntwarn.on{display:block}
.tog{display:flex;align-items:center;gap:8px;margin:8px 0 0}
.tog input{width:auto;margin:0}
.briwarn,.cntwarn{color:#f59e0b;font-size:.8rem;margin:4px 0 0}
#patchList{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch}
.patch{border:1px solid var(--border);border-radius:8px;margin:0 0 8px;background:var(--chrome)}
.patch.child{margin-left:14px;border-left:2px solid var(--accent)}
.patchhead{display:flex;align-items:center;gap:6px;padding:8px 8px 8px 10px;cursor:pointer}
.patchhead b{flex:1;min-width:0;font-size:.8rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.patchops{display:flex;gap:4px;flex:0 0 auto}
.patchops button{width:auto;margin:0;padding:4px 8px;font-size:.75rem}
.patchbody{display:none;padding:0 10px 10px}
.patch.open .patchbody{display:block}
#note{margin:6px 0 0;font-size:.85rem}
#status,#ver{flex:0 0 auto;margin:6px 0 0;font-size:.85rem;color:var(--muted);min-height:1.2em}
.ok{color:#86efac}
.err{color:#f87171}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}
.hint{color:var(--muted);font-size:.75rem;margin:0 0 6px;line-height:1.3}
</style>
</head>
<body>
<div class="strip">
<h1 id="nameView">dmxwhip</h1>
<input id="name" maxlength="63" autocomplete="off" aria-label="Device name">
<div class="row" style="margin-top:8px;justify-content:center">
<button class="pri" id="identify" type="button">Identify</button>
<button id="reboot" type="button">Reboot</button>
</div>
<p id="note"></p>
</div>
<div id="liveLock">Live input — Playback and Patch are locked</div>
<div class="tabs">
<button class="on" id="tabLive" type="button">Live</button>
<button id="tabPlay" type="button">Playback</button>
<button id="tabPixels" type="button">Patch</button>
<button id="tabSetup" type="button">Setup</button>
</div>
<div id="viewLive" class="on">
<div class="livehead"><div class="mode" id="mode"><i></i><span id="modeLab">idle</span></div></div>
<div class="dash">
<div class="tile">
<div class="clab">Radio</div>
<div class="tval" id="lrSsid">—</div>
<p class="tmono" id="lrSta">—</p>
<div class="sig">
<div class="bars" id="lrBars" data-n="0"><i></i><i></i><i></i><i></i></div>
<span class="tmono" id="lrRssi">—</span>
</div>
<p class="cap"><span id="lrAp">—</span> · saved <span id="lrSaved">—</span></p>
</div>
<div class="tile">
<div class="clab"><span id="lxProto">—</span> · <span id="lxSrc">—</span></div>
<div class="trio">
<div><b id="lxFps">—</b><em>fps</em></div>
<div><b id="lxPps">—</b><em>pps</em></div>
<div><b id="lxDrops">—</b><em>drop</em></div>
</div>
<p class="cap" id="lxCap"><span id="lxAge">—</span> · q <span id="lxQ">—</span> · buf <span id="lxBuf">—</span></p>
</div>
<div class="tile">
<div class="clab">Storage</div>
<div class="sbar"><i id="lsBar"></i></div>
<p class="cap"><span id="lsOk">—</span> · <span id="lsType">—</span> · <span id="lsSize">—</span></p>
<span id="lsUse" hidden></span>
</div>
<div class="tile">
<div class="clab">Show</div>
<div class="tval" id="lpNow">—</div>
<p class="tmono">frame <span id="lpFrame">—</span></p>
<div class="pips">
<span class="pip" id="lpPark"><i></i>parked</span>
<span class="pip" id="lpUr"><i></i>underrun</span>
</div>
</div>
</div>
<div class="health">
<p><span class="hi" id="lnBoard">—</span> · <span class="hi" id="lnChip">—</span> · bri <span class="hi" id="lnBri">—</span></p>
<p><span id="lhUp">—</span> · <span id="lhHeap">—</span> · <span id="lhPsram">—</span></p>
</div>
</div>
<div id="viewPlay">
<p class="hint">Idle plays SD. Live Art-Net/sACN preempts.</p>
<div id="plist"></div>
<p class="readout" id="playnow"></p>
<div class="row">
<button id="prev" type="button">Prev</button>
<button class="pri" id="play" type="button">Play</button>
<button id="stop" type="button">Stop</button>
<button id="next" type="button">Next</button>
<button id="rename" type="button">Rename</button>
</div>
<div id="renrow">
<input id="rendraft" maxlength="48" autocomplete="off">
<div class="row">
<button class="pri" id="renok" type="button">Save name</button>
<button id="rencancel" type="button">Cancel</button>
</div>
</div>
<div id="fileopts"><label class="lab" for="fileloop">Loop</label>
<select id="fileloop"><option value="one">This file</option><option value="all">All in this folder</option></select></div>
<div id="folderopts"><label class="lab" for="folderrep">Repeat</label>
<select id="folderrep"><option value="forever">Forever</option><option value="count">Set times</option></select>
<div id="foldernrow"><label class="lab" for="foldern">Times</label>
<input id="foldern" type="number" min="1" max="99" value="1" inputmode="numeric"></div></div>
</div>
<div id="viewPixels">
<p class="hint">Each row is a fixture. Same data GPIO chains under the parent (top of the group is first on the wire). Save writes the map. Brightness applies immediately.</p>
<div id="patchList"></div>
<div class="row patchbar">
<button class="pri" id="mapSave" type="button">Save</button>
<button id="mapAdd" type="button">Add Output +</button>
</div>
</div>
<div id="viewSetup">
<p class="hint">2.4 GHz only. If this sheet closes, rejoin <b>dmxwhip</b> or open http://4.3.2.1</p>
<div class="lab">Radio</div>
<div class="row">
<button id="scan" type="button">Scan</button>
<button class="pri" id="go" type="button">Connect</button>
<button id="forget" type="button">Forget</button>
</div>
<div id="list"></div>
<label class="lab" for="ssid">SSID</label>
<input id="ssid" maxlength="32" placeholder="SSID or hidden network" autocomplete="off">
<label class="lab" for="pass">Password</label>
<input id="pass" type="password" maxlength="63" placeholder="Leave empty if open" autocomplete="off">
<div id="savedrow"><p class="readout" id="savedlab"></p></div>
<p id="status"></p>
<div class="mod lab">Live input</div>
<div class="grid3">
<div><label class="lab" for="fps">FPS</label>
<select id="fps"><option value="20">20</option><option value="30">30</option><option value="40" selected>40</option><option value="60">60</option></select></div>
<div><label class="lab" for="buf">Buffer</label>
<select id="buf"><option value="0">0 latest</option><option value="1">1 frame</option><option value="2">2 frames</option><option value="3">3 frames</option></select></div>
</div>
<label class="lab" for="park">Hide AP if connected</label>
<select id="park"><option value="yes" selected>Yes</option><option value="no">No</option></select>
</div>
<p id="ver" class="readout">dmxwhip v0.20.1</p>
<script>
const list=document.getElementById('list');
const plist=document.getElementById('plist');
const ssidEl=document.getElementById('ssid');
const passEl=document.getElementById('pass');
const statusEl=document.getElementById('status');
const noteEl=document.getElementById('note');
const liveLock=document.getElementById('liveLock');
const savedRow=document.getElementById('savedrow');
const savedLab=document.getElementById('savedlab');
const verEl=document.getElementById('ver');
const nameView=document.getElementById('nameView');
const nameEl=document.getElementById('name');
const modeEl=document.getElementById('mode');
const modeLab=document.getElementById('modeLab');
const playnowEl=document.getElementById('playnow');
const fpsEl=document.getElementById('fps');
const bufEl=document.getElementById('buf');
const parkEl=document.getElementById('park');
const patchList=document.getElementById('patchList');
const mapAdd=document.getElementById('mapAdd');
const fileloopEl=document.getElementById('fileloop');
const folderrepEl=document.getElementById('folderrep');
const foldernEl=document.getElementById('foldern');
const fileopts=document.getElementById('fileopts');
const folderopts=document.getElementById('folderopts');
const foldernrow=document.getElementById('foldernrow');
const renrow=document.getElementById('renrow');
const rendraft=document.getElementById('rendraft');
const viewLive=document.getElementById('viewLive');
const viewPlay=document.getElementById('viewPlay');
const viewPixels=document.getElementById('viewPixels');
const viewSetup=document.getElementById('viewSetup');
const tabLive=document.getElementById('tabLive');
const tabPlay=document.getElementById('tabPlay');
const tabPixels=document.getElementById('tabPixels');
const tabSetup=document.getElementById('tabSetup');
const idBtn=document.getElementById('identify');
const rebootBtn=document.getElementById('reboot');
const mapSave=document.getElementById('mapSave');
const CLOCKED={apa102:1,sk9822:1,hd107s:1,ws2801:1,lpd8806:1,p9813:1,lpd6803:1};
const CHIP_CLK=[['ws2812b','WS2812B'],['ws2812','WS2812'],['ws2813','WS2813'],['ws2815','WS2815'],['ws2816','WS2816'],['ws2818','WS2818'],['ws2811','WS2811'],['sk6812','SK6812'],['sk6822','SK6822'],['tm1803','TM1803'],['tm1804','TM1804'],['tm1809','TM1809'],['tm1829','TM1829'],['ucs1903','UCS1903'],['ucs1903b','UCS1903B'],['ucs1904','UCS1904'],['ucs2903','UCS2903'],['apa106','APA106'],['pl9823','PL9823'],['sm16703','SM16703'],['ge8822','GE8822'],['gw6205','GW6205'],['gs1903','GS1903'],['lpd1886','LPD1886']];
const CHIP_CKD=[['apa102','APA102'],['sk9822','SK9822'],['hd107s','HD107S'],['ws2801','WS2801'],['lpd8806','LPD8806'],['p9813','P9813'],['lpd6803','LPD6803']];
const ORDERS={
  rgb:['grb','rgb','rbg','gbr','brg','bgr'],
  rgbw:['grbw','rgbw','grwb','wrgb','rbgw','gbrw','brgw','bgrw','wgrb','wrbg'],
  rgbc:['grbc','rgbc','grcb','crgb','rbgc','gbrc','brgc','bgrc','cgrb','crbg'],
  rgbwc:['grbwc','rgbwc','grbcw','rgbcw','wrgbc','wrgcb','bgrwc','bgrcw','crgbw','cgrbw']
};
function orderKey(w,c){return w&&c?'rgbwc':w?'rgbw':c?'rgbc':'rgb';}
function defaultSeg(){return {proto:'auto',chip:'ws2812b',data:14,clk:0,count:64,white:false,cct:false,order:'grb',uni:0,ch:1,bri:10};}
let patch=[defaultSeg()];
let patchCaps={max_out:8,max_seg:24,max_px:1024};
let openSeg=0;
let patchKey='';
function chipOpts(sel){
  let h='<optgroup label="Clockless">';
  CHIP_CLK.forEach(c=>{h+='<option value="'+c[0]+'"'+(sel===c[0]?' selected':'')+'>'+c[1]+'</option>';});
  h+='</optgroup><optgroup label="Clocked">';
  CHIP_CKD.forEach(c=>{h+='<option value="'+c[0]+'"'+(sel===c[0]?' selected':'')+'>'+c[1]+'</option>';});
  return h+'</optgroup>';
}
function orderOpts(w,c,keep){
  const list=ORDERS[orderKey(!!w,!!c)];
  const cur=String(keep||list[0]).toLowerCase();
  return list.map(o=>'<option value="'+o+'"'+(o===cur?' selected':'')+'>'+o.toUpperCase()+'</option>').join('');
}
function parentOf(rows,i){
  const pin=rows[i].data;
  for(let j=0;j<i;j++) if(rows[j].data===pin) return j;
  return i;
}
function isChild(rows,i){return parentOf(rows,i)!==i;}
function groupBounds(rows,i){
  const pin=rows[i].data;
  let a=i,b=i;
  while(a>0&&rows[a-1].data===pin) a--;
  while(b+1<rows.length&&rows[b+1].data===pin) b++;
  return [a,b];
}
function groupCounts(rows){
  const m={};
  rows.forEach(r=>{m[r.data]=(m[r.data]||0)+r.count;});
  return m;
}
function unusedGpio(rows){
  const used=new Set(rows.map(r=>r.data));
  const tryPins=[14,21,1,2,3,8,9,10,11,12,13,15,16,17,18,35,36,37,38,39,40,41,42,47,48];
  for(let i=0;i<tryPins.length;i++) if(!used.has(tryPins[i])) return tryPins[i];
  return 14;
}
function segsFromStatus(s){
  if(s.outputs&&s.outputs.length){
    const rows=[];
    s.outputs.forEach(o=>{
      (o.segs||[]).forEach(seg=>{
        const proto=seg.proto||'auto';
        rows.push({
          proto:proto,chip:o.chip||'ws2812b',data:o.data,clk:o.clk||0,
          count:seg.count||64,white:!!seg.white,cct:!!seg.cct,order:seg.order||'grb',
          uni:proto==='sacn'?(seg.sacn||1):(seg.artnet||0),ch:seg.ch||1,
          bri:seg.bri!=null?seg.bri:10
        });
      });
    });
    if(rows.length) return rows;
  }
  const m=s.map||{};
  return [{
    proto:m.proto||'auto',chip:m.chip||'ws2812b',data:m.data!=null?m.data:14,clk:m.clk||0,
    count:m.count||64,white:!!m.white,cct:!!m.cct,order:m.order||'grb',
    uni:m.artnet!=null?m.artnet:0,ch:m.ch||1,bri:m.bri!=null?m.bri:10
  }];
}
function uniHint(proto){
  return proto==='sacn'?'sACN universe (1-based).':'Art-Net 0-based. Resolume “1.1” is often 0.1.';
}
function uniReadout(row){
  const chPx=3+(row.white?1:0)+(row.cct?1:0);
  const art=row.proto==='sacn'?Math.max(0,row.uni-1):row.uni;
  const sacn=row.proto==='sacn'?row.uni:(row.uni+1);
  return (row.proto==='sacn'?'sACN '+sacn:'Art-Net '+art)+' · ch '+row.ch+' · ch/px '+chPx;
}
function setTxt(id,v){const el=document.getElementById(id);if(el) el.textContent=v==null||v===''?'—':String(v);}
let pollTimer=0,briTimer=0,liveTimer=0,mapTimer=0;
let briDirty=false,liveDirty=false,playDirty=false,nameDirty=false,mapDirty=false,scanning=false;
let playSrc='root',playPath='/',playListKey='',lastFiles=[];
let tab='live',setupScanned=false,lastName='dmxwhip';
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t||'';}
function setNote(t,cls){noteEl.className=t?(cls||'err')+' on':'';noteEl.textContent=t||'';}
function dropHint(){setNote('Page dropped. Rejoin dmxwhip and open http://4.3.2.1','err');}
function showTab(name){
  tab=name;
  viewLive.className=name==='live'?'on':'';
  viewPlay.className=name==='play'?'on':'';
  viewPixels.className=name==='pixels'?'on':'';
  viewSetup.className=name==='setup'?'on':'';
  tabLive.className=name==='live'?'on':'';
  tabPlay.className=name==='play'?'on':'';
  tabPixels.className=name==='pixels'?'on':'';
  tabSetup.className=name==='setup'?'on':'';
  if(name==='setup'&&!setupScanned){
    setupScanned=true;
    scan(true);
  }
  if(name==='play'||name==='setup') fetchStatus();
}
function displayName(p){
  const base=String(p||'').split('/').pop()||'';
  return base.replace(/\.dmx$/i,'').replace(/^\d{2}_/,'')||p;
}
function showBri(){}
function showPlayOpts(){
  fileopts.className=playSrc==='file'?'on':'';
  folderopts.className=playSrc==='folder'?'on':'';
  foldernrow.className=(playSrc==='folder'&&folderrepEl.value==='count')?'on':'';
}
function markPlaySel(){
  const kids=plist.children;
  for(let i=0;i<kids.length;i++){
    const el=kids[i];
    if(!el.dataset) continue;
    const on=el.dataset.src===playSrc&&(playSrc==='root'||el.dataset.path===playPath);
    el.className='playrow'+(on?' sel':'');
  }
}
function renderPlayList(p){
  const files=p.files||[];
  const dirs=p.dirs||[];
  lastFiles=files;
  const key=(p.src||'')+'|'+(p.path||'')+'|'+files.join('\n')+'|'+dirs.join('\n');
  if(key===playListKey){markPlaySel();return;}
  playListKey=key;
  plist.innerHTML='';
  function row(src,path,label,kind){
    const d=document.createElement('div');
    d.className='playrow';
    d.dataset.src=src;
    d.dataset.path=path;
    d.innerHTML='<span>'+escapeHtml(label)+'</span><span class="readout">'+kind+'</span>';
    d.onclick=()=>{
      playDirty=true;
      playSrc=src;
      playPath=path;
      renrow.className='';
      markPlaySel();
      showPlayOpts();
    };
    plist.appendChild(d);
  }
  row('root','/','All .dmx in /','default');
  dirs.forEach(d=>row('folder',d,displayName(d),'dir'));
  files.forEach(f=>row('file',f,displayName(f),'.dmx'));
  if(!files.length&&!dirs.length){
    const e=document.createElement('p');
    e.className='hint';
    e.textContent='No .dmx files.';
    plist.appendChild(e);
  }
  markPlaySel();
}
function applyPlay(s){
  const p=s.play;
  if(!p){playnowEl.textContent='Now stopped';return;}
  if(!playDirty){
    playSrc=p.src||'root';
    playPath=p.path||'/';
    if(p.file_loop) fileloopEl.value=p.file_loop;
    if(p.folder_rep) folderrepEl.value=p.folder_rep;
    if(typeof p.n==='number') foldernEl.value=String(p.n);
  }
  showPlayOpts();
  renderPlayList(p);
  playnowEl.textContent=p.now?'Now '+displayName(p.now):'Now stopped';
}
function applyLive(s){
  if(liveDirty) return;
  if(typeof s.fps==='number') fpsEl.value=String(s.fps);
  if(typeof s.buf==='number') bufEl.value=String(s.buf);
  if(s.park) parkEl.value=s.park;
}
function fmtUp(ms){
  const s=Math.floor((ms||0)/1000);
  const h=Math.floor(s/3600);
  const m=Math.floor((s%3600)/60);
  const sec=s%60;
  return (h?h+':':'')+String(m).padStart(h?2:1,'0')+':'+String(sec).padStart(2,'0');
}
function fmtKb(n){
  if(typeof n!=='number') return '—';
  return Math.round(n/1024)+'K';
}
function showName(n){
  if(!n) return;
  lastName=n;
  if(!nameDirty){
    nameView.textContent=n;
    nameEl.value=n;
  }
}
function applyChrome(s){
  if(s.ver) verEl.textContent='dmxwhip v'+s.ver;
  showName(s.name);
  const mode=s.mode||(s.live?'live':(s.play&&s.play.now?'play':'idle'));
  modeEl.className='mode '+mode;
  if(modeLab) modeLab.textContent=mode;
  const live=!!s.live;
  if(liveLock) liveLock.className=live?'on':'';
  ['prev','play','stop','next','rename'].forEach(id=>{const el=document.getElementById(id);if(el) el.disabled=live;});
  idBtn.disabled=live;
  idBtn.title=live?'Unavailable while live':'';
  if(mapSave) mapSave.disabled=live;
  if(mapAdd) mapAdd.disabled=live;
  if(s.saved){savedRow.className='on';savedLab.textContent='saved '+s.saved+' (connects at boot)'+(s.ip?(' · STA '+s.ip):'');}
  else {savedRow.className='';savedLab.textContent='';}
  applyLive(s);
}
function applyPixels(s){
  if(s.patch){
    patchCaps={
      max_out:s.patch.max_out||8,
      max_seg:s.patch.max_seg||24,
      max_px:s.patch.max_px||1024
    };
  }
  if(!mapDirty){
    const next=segsFromStatus(s);
    const key=JSON.stringify(next);
    if(key!==patchKey){
      patch=next;
      patchKey=key;
      renderPatch();
    }
  }
}
function applyStats(s){
  applyChrome(s);
  applyPixels(s);
  setTxt('lrSsid',s.ssid||'no STA');
  setTxt('lrSta',s.ip||'—');
  setTxt('lrRssi',typeof s.rssi==='number'?s.rssi+' dBm':'—');
  setTxt('lrAp',s.ap_ip||'AP off');
  setTxt('lrSaved',s.saved||'none');
  const bars=document.getElementById('lrBars');
  if(bars){
    let n=0,lvl='';
    if(typeof s.rssi==='number'){
      n=s.rssi>=-50?4:s.rssi>=-60?3:s.rssi>=-70?2:1;
      lvl=s.rssi>=-50?'hi':s.rssi>=-70?'mid':'lo';
    }
    bars.dataset.n=String(n);
    bars.className='bars'+(lvl&&lvl!=='hi'?' '+lvl:'');
  }
  setTxt('lnBoard',s.board||'—');
  setTxt('lnChip',s.chip||'—');
  setTxt('lnBri',typeof s.bri==='number'?s.bri:'—');
  const sd=s.sd;
  setTxt('lsOk',!sd?'no SD':(sd.ok?'mounted':'not mounted'));
  setTxt('lsType',sd&&sd.type?sd.type:'—');
  setTxt('lsSize',sd&&sd.size_mb!=null?sd.size_mb+' MB':'—');
  setTxt('lsUse',sd&&sd.used_mb!=null?sd.used_mb+' / '+sd.free_mb+' MB':'—');
  const bar=document.getElementById('lsBar');
  if(bar){
    const pct=(sd&&sd.used_mb!=null&&sd.size_mb)?Math.max(0,Math.min(100,sd.used_mb/sd.size_mb*100)):0;
    bar.style.width=pct+'%';
  }
  setTxt('lxProto',s.proto||'—');
  setTxt('lxSrc',s.src&&s.src!=='none'?s.src:'none');
  setTxt('lxFps',typeof s.fps==='number'?s.fps:'—');
  setTxt('lxBuf',typeof s.buf==='number'?s.buf:'—');
  setTxt('lxAge',typeof s.age_ms==='number'?s.age_ms+' ms':'—');
  setTxt('lxQ',typeof s.queued==='number'?s.queued:'—');
  setTxt('lxDrops',typeof s.drops==='number'?s.drops:'—');
  setTxt('lxPps',typeof s.pps==='number'?s.pps:'—');
  const cap=document.getElementById('lxCap');
  if(cap){
    const locked=s.src&&s.src!=='none';
    cap.className='cap'+(!locked?' idle':(typeof s.age_ms==='number'&&s.age_ms>1000?' stale':''));
  }
  const p=s.play||{};
  setTxt('lpNow',p.now?displayName(p.now):'stopped');
  const parkPip=document.getElementById('lpPark');
  if(parkPip) parkPip.className='pip'+(p.parked?' on':'');
  const urPip=document.getElementById('lpUr');
  if(urPip) urPip.className='pip'+(p.underrun?' warn':'');
  setTxt('lpFrame',typeof p.frame==='number'?p.frame:'—');
  setTxt('lhUp',typeof s.up_ms==='number'?fmtUp(s.up_ms):'—');
  setTxt('lhHeap',fmtKb(s.heap));
  setTxt('lhPsram',fmtKb(s.psram));
}
function applyMeta(s){
  applyChrome(s);
  applyPixels(s);
  applyPlay(s);
}
async function jget(url){
  const r=await fetch(url,{cache:'no-store'});
  if(!r.ok) throw new Error('http');
  return r.json();
}
function postForm(url,fields){
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(fields)});
}
function renderNets(nets){
  list.innerHTML='';
  (nets||[]).forEach(n=>{
    const d=document.createElement('div');
    d.className='net'+(ssidEl.value===n.ssid?' sel':'');
    d.innerHTML='<span>'+escapeHtml(n.ssid)+'</span><span class="readout">'+(n.secure?'lock ':'open ')+n.rssi+' dBm</span>';
    d.onclick=()=>{
      const kids=list.children;
      for(let i=0;i<kids.length;i++) kids[i].classList.remove('sel');
      d.classList.add('sel');
      ssidEl.value=n.ssid;
    };
    list.appendChild(d);
  });
  if(!nets||!nets.length) setStatus('No networks found. Try Scan again.');
}
function escapeHtml(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
async function scan(force){
  scanning=true;
  setStatus('Scanning…');
  try{
    await jget('/scan'+(force?'?start=1':''));
    let finished=false;
    for(let i=0;i<25;i++){
      const data=await jget('/scan');
      if(data.state==='scanning'){await new Promise(r=>setTimeout(r,400));continue;}
      if(data.state==='connecting'){setStatus('Connect in progress…');finished=true;break;}
      renderNets(data.networks||[]);
      if((data.networks||[]).length) setStatus('Select a network, then Connect.');
      finished=true;
      break;
    }
    if(!finished) setStatus('Scan timed out. Try again.','err');
  }catch(e){dropHint();}
  finally{scanning=false;}
  try{
    const s=await jget('/status');
    if(s.state==='connected'||s.state==='connecting'||s.state==='failed') showStatus(s);
  }catch(e){}
}
function showStatus(s){
  applyChrome(s);
  if(s.play&&(s.play.files||s.play.dirs)) applyPlay(s);
  if(scanning) return s.state==='connecting';
  if(s.state==='connected'){
    setStatus('Connected to '+s.ssid+'  STA IP '+s.ip+(s.ap_ip?'  (AP '+s.ap_ip+')':''),'ok');
    return false;
  }
  if(s.state==='connecting'){setStatus('Connecting to '+s.ssid+'…');return true;}
  if(s.state==='failed'){setStatus('Failed'+(s.ssid?' ('+s.ssid+')':'')+(s.error?': '+s.error:'')+'. SoftAP is still up — try again.','err');return false;}
  if(s.state==='scanning'){setStatus('Scanning…');return true;}
  return false;
}
async function fetchStatus(){
  try{
    const s=await jget('/status');
    showStatus(s);
  }catch(e){dropHint();}
}
async function poll(){
  try{
    const s=await jget('/api/stats');
    if(noteEl.textContent.indexOf('Page dropped')===0) setNote('');
    applyStats(s);
    showStatus(s);
    if(tab==='play'||tab==='setup') await fetchStatus();
    const fast=s.state==='connecting'||s.state==='scanning';
    pollTimer=setTimeout(poll,fast?500:1000);
  }catch(e){dropHint();}
}
async function connect(){
  const ssid=ssidEl.value.trim();
  const password=passEl.value;
  if(!ssid){setStatus('Enter a network name.','err');return;}
  try{
    const r=await postForm('/connect',{ssid,password});
    const s=await r.json();
    applyMeta(s);
    if(!r.ok){setStatus(s.error||'Connect rejected','err');return;}
    showStatus(s);
    clearTimeout(pollTimer);
    poll();
  }catch(e){dropHint();}
}
async function forget(){
  try{
    const r=await fetch('/forget',{method:'POST'});
    const s=await r.json();
    applyMeta(s);
    setStatus('Saved network forgotten. SoftAP is still up.');
  }catch(e){dropHint();}
}
function postBri(v,i){
  const fields={v:String(v)};
  if(i!=null) fields.i=String(i);
  postForm('/brightness',fields).then(()=>{briDirty=false;}).catch(dropHint);
}
function scheduleBri(v,i){
  v=Math.max(0,Math.min(255,v|0));
  briDirty=true;
  if(patch[i]) patch[i].bri=v;
  clearTimeout(briTimer);
  briTimer=setTimeout(()=>postBri(v,i),300);
}
function postLive(){
  postForm('/live',{fps:fpsEl.value,buf:bufEl.value,park:parkEl.value})
    .then(async r=>{if(!r.ok) throw new Error('http');liveDirty=false;applyMeta(await r.json());})
    .catch(dropHint);
}
function scheduleLive(){
  liveDirty=true;
  clearTimeout(liveTimer);
  liveTimer=setTimeout(postLive,300);
}
function readPatchDom(){
  const cards=patchList.querySelectorAll('.patch');
  cards.forEach(card=>{
    const i=+card.dataset.i;
    if(!patch[i]) return;
    const g=id=>card.querySelector('[data-f="'+id+'"]');
    const proto=g('proto'); const chip=g('chip'); const data=g('data'); const clk=g('clk');
    const count=g('count'); const white=g('white'); const cct=g('cct'); const order=g('order');
    const uni=g('uni'); const ch=g('ch'); const bri=g('bri');
    if(proto) patch[i].proto=proto.value;
    if(chip) patch[i].chip=chip.value;
    if(data) patch[i].data=Math.max(0,Math.min(48,parseInt(data.value,10)||0));
    if(clk) patch[i].clk=Math.max(0,Math.min(48,parseInt(clk.value,10)||0));
    if(count) patch[i].count=Math.max(1,Math.min(patchCaps.max_px,parseInt(count.value,10)||1));
    if(white) patch[i].white=white.checked;
    if(cct) patch[i].cct=cct.checked;
    if(order) patch[i].order=order.value;
    if(uni) patch[i].uni=Math.max(0,Math.min(32767,parseInt(uni.value,10)||0));
    if(ch) patch[i].ch=Math.max(1,Math.min(512,parseInt(ch.value,10)||1));
    if(bri) patch[i].bri=Math.max(0,Math.min(255,parseInt(bri.value,10)||0));
  });
  patch.forEach((row,i)=>{
    const p=parentOf(patch,i);
    if(p!==i){row.chip=patch[p].chip;row.clk=patch[p].clk;row.data=patch[p].data;}
  });
}
function postMap(){
  readPatchDom();
  const fields={n:String(patch.length)};
  patch.forEach((row,i)=>{
    fields['proto'+i]=row.proto;
    fields['chip'+i]=row.chip;
    fields['data'+i]=String(row.data);
    fields['clk'+i]=String(row.clk||0);
    fields['count'+i]=String(row.count);
    fields['white'+i]=row.white?'1':'0';
    fields['cct'+i]=row.cct?'1':'0';
    fields['order'+i]=row.order;
    fields['uni'+i]=String(row.uni);
    fields['ch'+i]=String(row.ch);
    fields['bri'+i]=String(row.bri);
  });
  return postForm('/map',fields).then(async r=>{
      const s=await r.json().catch(()=>({}));
      if(!r.ok){setNote(s.error||'Map save failed','err');return false;}
      mapDirty=false;
      setNote('Saved. Rebooting…');
      applyMeta(s);
      return true;
    });
}
function markPatch(){mapDirty=true;}
function addOutput(){
  readPatchDom();
  if(patch.length>=patchCaps.max_seg){setNote('Segment cap '+patchCaps.max_seg,'err');return;}
  const pins=new Set(patch.map(r=>r.data));
  if(pins.size>=patchCaps.max_out){setNote('Output cap '+patchCaps.max_out,'err');return;}
  const row=defaultSeg();
  row.data=unusedGpio(patch);
  patch.push(row);
  openSeg=patch.length-1;
  markPatch();
  renderPatch();
}
function addOnPin(parent){
  readPatchDom();
  if(patch.length>=patchCaps.max_seg){setNote('Segment cap '+patchCaps.max_seg,'err');return;}
  const src=patch[parent];
  const row=Object.assign({},src);
  patch.splice(parent+1,0,row);
  openSeg=parent+1;
  markPatch();
  renderPatch();
}
function moveSeg(i,dir){
  readPatchDom();
  const j=i+dir;
  if(j<0||j>=patch.length) return;
  if(patch[i].data!==patch[j].data) return;
  const t=patch[i]; patch[i]=patch[j]; patch[j]=t;
  openSeg=j;
  markPatch();
  renderPatch();
}
function removeSeg(i){
  readPatchDom();
  if(patch.length<=1) return;
  patch.splice(i,1);
  openSeg=Math.min(openSeg,patch.length-1);
  markPatch();
  renderPatch();
}
function renderPatch(){
  if(!patchList) return;
  const totals=groupCounts(patch);
  const live=!!(idBtn&&idBtn.disabled);
  patchList.innerHTML='';
  patch.forEach((row,i)=>{
    const child=isChild(patch,i);
    const [g0,g1]=groupBounds(patch,i);
    const card=document.createElement('div');
    card.className='patch'+(child?' child':'')+(openSeg===i?' open':'');
    card.dataset.i=String(i);
    const title='GPIO '+row.data+' · '+(row.chip||'').toUpperCase()+' · '+(child?row.count:totals[row.data])+' px';
    const locked=child?' disabled':'';
    let ops='';
    if(child){
      const firstChild=g0+1;
      const many=g1>firstChild;
      if(many&&i>firstChild) ops+='<button class="icon" type="button" data-a="up" aria-label="Move up">▲</button>';
      if(many&&i<g1) ops+='<button class="icon" type="button" data-a="down" aria-label="Move down">▼</button>';
      ops+='<button class="icon" type="button" data-a="del" aria-label="Delete">🗑</button>';
    }else{
      ops+='<button type="button" data-a="seg">+</button>';
      if(patch.length>1) ops+='<button class="icon" type="button" data-a="del" aria-label="Delete">🗑</button>';
    }
    card.innerHTML=
      '<div class="patchhead"><b>'+escapeHtml(title)+'</b><div class="patchops">'+ops+
      '</div></div><div class="patchbody">'+
      '<label class="lab">Live protocol</label><select data-f="proto">'+
      '<option value="auto"'+(row.proto==='auto'?' selected':'')+'>Auto</option>'+
      '<option value="artnet"'+(row.proto==='artnet'?' selected':'')+'>Art-Net</option>'+
      '<option value="sacn"'+(row.proto==='sacn'?' selected':'')+'>sACN</option></select>'+
      '<label class="lab">IC type</label><select data-f="chip"'+locked+'>'+chipOpts(row.chip)+'</select>'+
      '<label class="lab">Data GPIO</label><input data-f="data" type="number" min="0" max="48" value="'+row.data+'" inputmode="numeric"'+locked+'>'+
      '<div class="clkrow'+(CLOCKED[row.chip]?' on':'')+'"><label class="lab">Clock GPIO</label>'+
      '<input data-f="clk" type="number" min="1" max="48" value="'+(row.clk||21)+'" inputmode="numeric"'+locked+'></div>'+
      '<label class="lab">Pixel count</label><input data-f="count" type="number" min="1" max="'+patchCaps.max_px+'" value="'+row.count+'" inputmode="numeric">'+
      '<p class="cntwarn'+(row.count!==64?' on':'')+'">This board’s panel is 64 pixels (8×8).</p>'+
      '<label class="tog"><input data-f="white" type="checkbox"'+(row.white?' checked':'')+'> White channel</label>'+
      '<label class="tog"><input data-f="cct" type="checkbox"'+(row.cct?' checked':'')+'> CCT channel</label>'+
      '<label class="lab">Color order</label><select data-f="order">'+orderOpts(row.white,row.cct,row.order)+'</select>'+
      '<label class="lab">Starting universe</label><input data-f="uni" type="number" min="0" max="32767" value="'+row.uni+'" inputmode="numeric">'+
      '<p class="hint">'+uniHint(row.proto)+'</p>'+
      '<label class="lab">Starting channel</label><input data-f="ch" type="number" min="1" max="512" value="'+row.ch+'" inputmode="numeric">'+
      '<label class="lab">Brightness</label><div class="brirow">'+
      '<input data-f="bri" type="range" min="0" max="255" value="'+row.bri+'">'+
      '<input data-f="brinum" type="number" min="0" max="255" value="'+row.bri+'" inputmode="numeric"></div>'+
      '<p class="briwarn'+(row.bri>64?' on':'')+'">This 8×8 can overheat above 64.</p>'+
      '<p class="readout">'+escapeHtml(uniReadout(row))+'</p></div>';
    const head=card.querySelector('.patchhead');
    head.onclick=e=>{
      if(e.target.closest('.patchops')) return;
      openSeg=openSeg===i?-1:i;
      renderPatch();
    };
    card.querySelectorAll('.patchops button').forEach(btn=>{
      btn.onclick=e=>{
        e.stopPropagation();
        const a=btn.dataset.a;
        if(a==='seg') addOnPin(i);
        else if(a==='up') moveSeg(i,-1);
        else if(a==='down') moveSeg(i,1);
        else if(a==='del') removeSeg(i);
      };
      btn.disabled=live;
    });
    card.querySelectorAll('input,select').forEach(el=>{
      el.disabled=live||el.disabled;
      const f=el.getAttribute('data-f');
      if(f==='bri'||f==='brinum'){
        el.oninput=()=>{
          const v=Math.max(0,Math.min(255,parseInt(el.value,10)||0));
          const range=card.querySelector('[data-f="bri"]');
          const num=card.querySelector('[data-f="brinum"]');
          if(range) range.value=String(v);
          if(num) num.value=String(v);
          const warn=card.querySelector('.briwarn');
          if(warn) warn.className='briwarn'+(v>64?' on':'');
          scheduleBri(v,i);
        };
        return;
      }
      el.oninput=el.onchange=()=>{
        markPatch();
        if(f==='white'||f==='cct'){
          const w=card.querySelector('[data-f="white"]');
          const c=card.querySelector('[data-f="cct"]');
          const ord=card.querySelector('[data-f="order"]');
          if(ord) ord.innerHTML=orderOpts(w&&w.checked,c&&c.checked,ord.value);
        }
        if(f==='chip'){
          const clk=card.querySelector('.clkrow');
          if(clk) clk.className='clkrow'+(CLOCKED[el.value]?' on':'');
        }
        if(f==='data'&&!child){
          readPatchDom();
          renderPatch();
          return;
        }
        if(f==='proto'){
          const hint=card.querySelector('.hint');
          if(hint) hint.textContent=uniHint(el.value);
        }
        const warn=card.querySelector('.cntwarn');
        const cnt=card.querySelector('[data-f="count"]');
        if(warn&&cnt) warn.className='cntwarn'+(parseInt(cnt.value,10)!==64?' on':'');
      };
    });
    patchList.appendChild(card);
  });
  if(mapAdd) mapAdd.disabled=live||patch.length>=patchCaps.max_seg;
}
function savePatch(){
  mapSave.textContent='Saving…';
  mapSave.disabled=true;
  const jobs=[];
  if(mapDirty) jobs.push(postMap());
  if(liveDirty) jobs.push(postLive());
  Promise.all(jobs).then(results=>{
    if(results.some(v=>v===true)){
      mapSave.textContent='Rebooting…';
      return;
    }
    if(!mapDirty&&!liveDirty) setNote('');
    mapSave.textContent='Save';
    mapSave.disabled=!!(idBtn&&idBtn.disabled);
  }).catch(e=>{
    mapSave.textContent='Save';
    mapSave.disabled=!!(idBtn&&idBtn.disabled);
    dropHint();
  });
}
function parsePlayN(){
  const t=foldernEl.value.trim();
  if(!/^\d+$/.test(t)) return 1;
  return Math.max(1,Math.min(99,parseInt(t,10)));
}
function postPlay(src,path){
  const n=parsePlayN();
  foldernEl.value=String(n);
  playSrc=src||playSrc;
  playPath=path||playPath;
  return postForm('/play',{
    src:playSrc,path:playPath,file_loop:fileloopEl.value,folder_rep:folderrepEl.value,n:String(n)
  }).then(async r=>{
    if(!r.ok) throw new Error('http');
    playDirty=false;
    applyMeta(await r.json());
  }).catch(dropHint);
}
function adjacent(step){
  if(!lastFiles.length) return null;
  const cur=playSrc==='file'&&lastFiles.indexOf(playPath)>=0?playPath:lastFiles[0];
  const i=lastFiles.indexOf(cur);
  return lastFiles[(i+step+lastFiles.length)%lastFiles.length];
}
function startEdit(){
  nameDirty=true;
  nameEl.value=lastName;
  document.body.classList.add('editing');
  nameEl.focus();
  nameEl.select();
}
function endEdit(save){
  if(!document.body.classList.contains('editing')) return;
  document.body.classList.remove('editing');
  const long=nameEl.value.trim();
  if(!save||!long){
    nameEl.value=lastName;
    nameView.textContent=lastName;
    nameDirty=false;
    if(save&&!long) setNote('Enter a device name','err');
    return;
  }
  if(long===lastName){nameDirty=false;return;}
  postForm('/name',{long}).then(async r=>{
    if(!r.ok) throw new Error('http');
    nameDirty=false;
    setNote('');
    const s=await r.json();
    applyChrome(s);
  }).catch(()=>{nameDirty=false;nameEl.value=lastName;nameView.textContent=lastName;dropHint();});
}
fpsEl.onchange=scheduleLive;
bufEl.onchange=scheduleLive;
parkEl.onchange=scheduleLive;
mapSave.onclick=savePatch;
if(mapAdd) mapAdd.onclick=addOutput;
folderrepEl.onchange=showPlayOpts;
tabLive.onclick=()=>showTab('live');
tabPlay.onclick=()=>showTab('play');
tabPixels.onclick=()=>showTab('pixels');
tabSetup.onclick=()=>showTab('setup');
document.getElementById('scan').onclick=()=>scan(true);
document.getElementById('go').onclick=connect;
document.getElementById('forget').onclick=forget;
document.getElementById('play').onclick=()=>postPlay();
document.getElementById('stop').onclick=()=>postForm('/play',{src:'stop'}).then(async r=>{if(!r.ok) throw new Error('http');applyMeta(await r.json());}).catch(dropHint);
document.getElementById('prev').onclick=()=>{const t=adjacent(-1);if(t){playDirty=true;playSrc='file';playPath=t;markPlaySel();showPlayOpts();postPlay('file',t);}};
document.getElementById('next').onclick=()=>{const t=adjacent(1);if(t){playDirty=true;playSrc='file';playPath=t;markPlaySel();showPlayOpts();postPlay('file',t);}};
nameView.onclick=startEdit;
nameEl.onkeydown=e=>{
  if(e.key==='Enter'){e.preventDefault();nameEl.blur();}
  if(e.key==='Escape'){e.preventDefault();endEdit(false);}
};
nameEl.onblur=()=>endEdit(true);
idBtn.onclick=()=>{
  idBtn.textContent='Identifying…';
  postForm('/identify',{ms:'3000'}).then(async r=>{
    idBtn.textContent='Identify';
    if(!r.ok){const s=await r.json().catch(()=>({}));setNote(s.error||'Identify failed','err');return;}
    setNote('');
  }).catch(()=>{idBtn.textContent='Identify';dropHint();});
};
rebootBtn.onclick=()=>{
  if(!window.confirm('Reboot this node?')) return;
  rebootBtn.disabled=true;
  postForm('/reboot',{}).then(async r=>{
    if(!r.ok){rebootBtn.disabled=false;const s=await r.json().catch(()=>({}));setNote(s.error||'Reboot failed','err');return;}
    setNote('Rebooting…');
  }).catch(()=>{rebootBtn.disabled=false;dropHint();});
};
document.getElementById('rename').onclick=()=>{
  if(playSrc!=='file'||!playPath) return;
  rendraft.value=displayName(playPath);
  renrow.className='on';
};
document.getElementById('rencancel').onclick=()=>{renrow.className='';};
document.getElementById('renok').onclick=()=>{
  if(playSrc!=='file'||!playPath) return;
  const raw=rendraft.value.trim().replace(/[<>:"/\\|?*\u0000-\u001f]/g,'').replace(/[. ]+$/,'')||'show';
  const base=playPath.split('/').pop()||'';
  const prefix=/^\d{2}_/.test(base)?base.slice(0,3):'';
  const dir=playPath.slice(0,playPath.lastIndexOf('/'))||'';
  const to=dir+'/'+prefix+raw+'.dmx';
  postForm('/rename',{from:playPath,to}).then(async r=>{
    const s=await r.json();
    if(!r.ok){setNote(s.error||'Rename failed','err');return;}
    playDirty=false;
    renrow.className='';
    setNote('');
    applyMeta(s);
  }).catch(dropHint);
};
renderPatch();
(async()=>{
  try{
    const s=await jget('/api/stats');
    applyStats(s);
    showStatus(s);
    if(s.ip) showTab('live');
    else showTab('setup');
    poll();
  }catch(e){dropHint();}
})();
</script>
</body>
</html>
)WIFIHTML";
