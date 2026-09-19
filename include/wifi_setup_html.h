#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>dmxwhip v0.17.1</title>
<style>
:root{--bg:#09090b;--chrome:#18181b;--border:#27272a;--text:#e4e4e7;--muted:#71717a;--accent:#22d3ee}
html,body{height:100%;height:100dvh;margin:0;overflow:hidden}
body{display:flex;flex-direction:column;box-sizing:border-box;padding:10px 12px;font-family:system-ui,sans-serif;background:var(--bg);color:var(--text);max-width:28rem;margin:0 auto}
.strip{flex:0 0 auto;padding:0 0 8px;margin:0 0 8px;border-bottom:1px solid var(--border);text-align:center}
#nameView{margin:0;font-size:1.35rem;font-weight:700;cursor:pointer;word-break:break-word}
#name{display:none;text-align:center;font-weight:700}
body.editing #name{display:block}
body.editing #nameView{display:none}
#identify{width:auto;min-width:7rem;margin:8px auto 0;display:inline-block}
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
#savedrow,#renrow,#fileopts,#folderopts,#foldernrow,#briwarn,#cntwarn,#note,#clkrow{display:none}
#savedrow.on,#renrow.on,#fileopts.on,#folderopts.on,#foldernrow.on,#briwarn.on,#cntwarn.on,#note.on,#clkrow.on{display:block}
.tog{display:flex;align-items:center;gap:8px;margin:8px 0 0}
.tog input{width:auto;margin:0}
#briwarn,#cntwarn{color:#f59e0b;font-size:.8rem;margin:4px 0 0}
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
<button class="pri" id="identify" type="button">Identify</button>
<p id="note"></p>
</div>
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
<p class="hint">Patch: IC, wire order, start universe/channel. Save writes the map. Brightness applies immediately.</p>
<label class="lab" for="proto">Live protocol</label>
<select id="proto"><option value="auto">Auto</option><option value="artnet">Art-Net</option><option value="sacn">sACN</option></select>
<label class="lab" for="pxChip">IC type</label>
<select id="pxChip">
<optgroup label="Clockless">
<option value="ws2812b">WS2812B</option>
<option value="ws2812">WS2812</option>
<option value="ws2813">WS2813</option>
<option value="ws2815">WS2815</option>
<option value="ws2816">WS2816</option>
<option value="ws2818">WS2818</option>
<option value="ws2811">WS2811</option>
<option value="sk6812">SK6812</option>
<option value="sk6822">SK6822</option>
<option value="tm1803">TM1803</option>
<option value="tm1804">TM1804</option>
<option value="tm1809">TM1809</option>
<option value="tm1829">TM1829</option>
<option value="ucs1903">UCS1903</option>
<option value="ucs1903b">UCS1903B</option>
<option value="ucs1904">UCS1904</option>
<option value="ucs2903">UCS2903</option>
<option value="apa106">APA106</option>
<option value="pl9823">PL9823</option>
<option value="sm16703">SM16703</option>
<option value="ge8822">GE8822</option>
<option value="gw6205">GW6205</option>
<option value="gs1903">GS1903</option>
<option value="lpd1886">LPD1886</option>
</optgroup>
<optgroup label="Clocked">
<option value="apa102">APA102</option>
<option value="sk9822">SK9822</option>
<option value="hd107s">HD107S</option>
<option value="ws2801">WS2801</option>
<option value="lpd8806">LPD8806</option>
<option value="p9813">P9813</option>
<option value="lpd6803">LPD6803</option>
</optgroup>
</select>
<label class="lab" for="pxData">Data GPIO</label>
<input id="pxData" type="number" min="0" max="48" value="14" inputmode="numeric">
<div id="clkrow"><label class="lab" for="pxClk">Clock GPIO</label>
<input id="pxClk" type="number" min="1" max="48" value="21" inputmode="numeric"></div>
<label class="lab" for="pxCount">Pixel count</label>
<input id="pxCount" type="number" min="1" max="1024" value="64" inputmode="numeric">
<p id="cntwarn">This board’s panel is 64 pixels (8×8).</p>
<label class="tog"><input id="pxWhite" type="checkbox"> White channel</label>
<label class="tog"><input id="pxCct" type="checkbox"> CCT channel</label>
<label class="lab" for="pxOrder">Color order</label>
<select id="pxOrder"></select>
<label class="lab" for="pxUniStart">Starting universe</label>
<input id="pxUniStart" type="number" min="0" max="32767" value="0" inputmode="numeric">
<p class="hint">Art-Net 0-based. sACN is this + 1.</p>
<label class="lab" for="pxCh">Starting channel</label>
<input id="pxCh" type="number" min="1" max="512" value="1" inputmode="numeric">
<label class="lab" for="bri">Brightness</label>
<div class="brirow">
<input id="bri" type="range" min="0" max="255" value="10">
<input id="brinum" type="number" min="0" max="255" value="10" inputmode="numeric">
</div>
<p id="briwarn">This 8×8 can overheat above 64.</p>
<div class="lab">Universes</div>
<p class="readout" id="pxUni"></p>
<button class="pri" id="mapSave" type="button">Save</button>
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
<p id="ver" class="readout">dmxwhip v0.17.1</p>
<script>
const list=document.getElementById('list');
const plist=document.getElementById('plist');
const ssidEl=document.getElementById('ssid');
const passEl=document.getElementById('pass');
const statusEl=document.getElementById('status');
const noteEl=document.getElementById('note');
const savedRow=document.getElementById('savedrow');
const savedLab=document.getElementById('savedlab');
const verEl=document.getElementById('ver');
const nameView=document.getElementById('nameView');
const nameEl=document.getElementById('name');
const modeEl=document.getElementById('mode');
const modeLab=document.getElementById('modeLab');
const briEl=document.getElementById('bri');
const brinum=document.getElementById('brinum');
const briwarn=document.getElementById('briwarn');
const playnowEl=document.getElementById('playnow');
const protoEl=document.getElementById('proto');
const fpsEl=document.getElementById('fps');
const bufEl=document.getElementById('buf');
const parkEl=document.getElementById('park');
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
const pxChip=document.getElementById('pxChip');
const pxOrder=document.getElementById('pxOrder');
const pxCount=document.getElementById('pxCount');
const pxData=document.getElementById('pxData');
const pxClk=document.getElementById('pxClk');
const pxWhite=document.getElementById('pxWhite');
const pxCct=document.getElementById('pxCct');
const pxUniStart=document.getElementById('pxUniStart');
const pxCh=document.getElementById('pxCh');
const clkrow=document.getElementById('clkrow');
const pxUni=document.getElementById('pxUni');
const mapSave=document.getElementById('mapSave');
const cntwarn=document.getElementById('cntwarn');
const CLOCKED={apa102:1,sk9822:1,hd107s:1,ws2801:1,lpd8806:1,p9813:1,lpd6803:1};
const ORDERS={
  rgb:['grb','rgb','rbg','gbr','brg','bgr'],
  rgbw:['grbw','rgbw','grwb','wrgb','rbgw','gbrw','brgw','bgrw','wgrb','wrbg'],
  rgbc:['grbc','rgbc','grcb','crgb','rbgc','gbrc','brgc','bgrc','cgrb','crbg'],
  rgbwc:['grbwc','rgbwc','grbcw','rgbcw','wrgbc','wrgcb','bgrwc','bgrcw','crgbw','cgrbw']
};
function orderKey(w,c){return w&&c?'rgbwc':w?'rgbw':c?'rgbc':'rgb';}
function fillOrders(keep){
  const list=ORDERS[orderKey(pxWhite.checked,pxCct.checked)];
  const cur=(keep||pxOrder.value||'').toLowerCase();
  pxOrder.innerHTML='';
  list.forEach(o=>{
    const opt=document.createElement('option');
    opt.value=o;
    opt.textContent=o.toUpperCase();
    pxOrder.appendChild(opt);
  });
  pxOrder.value=list.indexOf(cur)>=0?cur:list[0];
}
function showClk(){clkrow.className=CLOCKED[pxChip.value]?'on':'';}
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
function showBri(v){
  briEl.value=v;
  brinum.value=v;
  briwarn.className=v>64?'on':'';
}
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
  if(s.proto) protoEl.value=s.proto;
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
  ['prev','play','stop','next','rename'].forEach(id=>{const el=document.getElementById(id);if(el) el.disabled=live;});
  idBtn.disabled=live;
  idBtn.title=live?'Unavailable while live':'';
  [pxChip,pxOrder,pxCount,pxData,pxClk,pxWhite,pxCct,pxUniStart,pxCh,mapSave].forEach(el=>{if(el) el.disabled=live;});
  if(s.saved){savedRow.className='on';savedLab.textContent='saved '+s.saved+' (connects at boot)'+(s.ip?(' · STA '+s.ip):'');}
  else {savedRow.className='';savedLab.textContent='';}
  if(typeof s.bri==='number'&&!briDirty) showBri(s.bri);
  applyLive(s);
}
function applyPixels(s){
  const m=s.map||{};
  if(!mapDirty){
    if(m.chip) pxChip.value=m.chip;
    pxWhite.checked=!!m.white;
    pxCct.checked=!!m.cct;
    fillOrders(m.order||'');
    if(typeof m.count==='number') pxCount.value=String(m.count);
    if(typeof m.data==='number') pxData.value=String(m.data);
    if(typeof m.clk==='number'&&m.clk) pxClk.value=String(m.clk);
    if(typeof m.artnet==='number') pxUniStart.value=String(m.artnet);
    if(typeof m.ch==='number') pxCh.value=String(m.ch);
    showClk();
  }
  const n=parseInt(pxCount.value,10);
  cntwarn.className=(Number.isFinite(n)&&n!==64)?'on':'';
  const chPx=m.ch_px||(3+(m.white?1:0)+(m.cct?1:0));
  const fit=m.fit!=null?m.fit:Math.floor((512-((m.ch||1)-1))/chPx);
  const span=m.span!=null?m.span:1;
  pxUni.textContent=[
    m.artnet!=null?('Art-Net '+m.artnet):'',
    m.sacn!=null?('sACN '+m.sacn):'',
    m.ch!=null?('ch '+m.ch):'',
    chPx?('ch/px '+chPx):'',
    fit!=null?('first uni '+fit+' px'):'',
    span>1?('span '+span):'',
    m.split?'split universes':''
  ].filter(Boolean).join(' · ')||'—';
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
function postBri(v){
  postForm('/brightness',{v:String(v)}).then(()=>{briDirty=false;}).catch(dropHint);
}
function scheduleBri(v){
  v=Math.max(0,Math.min(255,v|0));
  briDirty=true;
  showBri(v);
  clearTimeout(briTimer);
  briTimer=setTimeout(()=>postBri(v),300);
}
function parseBriNum(){
  const t=brinum.value.trim();
  if(t===''||!/^\d+$/.test(t)) return null;
  return parseInt(t,10);
}
briEl.oninput=()=>scheduleBri(+briEl.value);
brinum.oninput=()=>{const n=parseBriNum();if(n!==null) scheduleBri(n);};
brinum.onchange=()=>{const n=parseBriNum();if(n===null) showBri(+briEl.value); else scheduleBri(n);};
function postLive(){
  postForm('/live',{proto:protoEl.value,fps:fpsEl.value,buf:bufEl.value,park:parkEl.value})
    .then(async r=>{if(!r.ok) throw new Error('http');liveDirty=false;applyMeta(await r.json());})
    .catch(dropHint);
}
function scheduleLive(){
  liveDirty=true;
  clearTimeout(liveTimer);
  liveTimer=setTimeout(postLive,300);
}
function postMap(){
  const count=Math.max(1,Math.min(1024,parseInt(pxCount.value,10)||64));
  const data=Math.max(0,Math.min(48,parseInt(pxData.value,10)||0));
  const clk=Math.max(0,Math.min(48,parseInt(pxClk.value,10)||0));
  const uni=Math.max(0,Math.min(32767,parseInt(pxUniStart.value,10)||0));
  const ch=Math.max(1,Math.min(512,parseInt(pxCh.value,10)||1));
  pxCount.value=String(count);
  pxData.value=String(data);
  pxClk.value=String(clk||21);
  pxUniStart.value=String(uni);
  pxCh.value=String(ch);
  cntwarn.className=count!==64?'on':'';
  fillOrders(pxOrder.value);
  return postForm('/map',{
    chip:pxChip.value,order:pxOrder.value,data:String(data),clk:String(clk),
    count:String(count),white:pxWhite.checked?'1':'0',cct:pxCct.checked?'1':'0',
    uni:String(uni),ch:String(ch)
  }).then(async r=>{
      const s=await r.json().catch(()=>({}));
      if(!r.ok){setNote(s.error||'Map save failed','err');return;}
      mapDirty=false;
      setNote('');
      applyMeta(s);
    });
}
function markPatch(){mapDirty=true;}
function savePatch(){
  mapSave.textContent='Saving…';
  mapSave.disabled=true;
  const jobs=[];
  if(mapDirty) jobs.push(postMap());
  if(liveDirty) jobs.push(postLive());
  Promise.all(jobs).then(()=>{
    if(!mapDirty&&!liveDirty) setNote('');
  }).catch(dropHint).finally(()=>{
    mapSave.textContent='Save';
    mapSave.disabled=!!(idBtn&&idBtn.disabled);
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
protoEl.onchange=()=>{liveDirty=true;};
fpsEl.onchange=scheduleLive;
bufEl.onchange=scheduleLive;
parkEl.onchange=scheduleLive;
pxChip.onchange=()=>{showClk();markPatch();};
pxOrder.onchange=markPatch;
pxCount.oninput=markPatch;
pxData.oninput=markPatch;
pxClk.oninput=markPatch;
pxUniStart.oninput=markPatch;
pxCh.oninput=markPatch;
pxWhite.onchange=()=>{fillOrders();markPatch();};
pxCct.onchange=()=>{fillOrders();markPatch();};
mapSave.onclick=savePatch;
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
fillOrders();
showClk();
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
