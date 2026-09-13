#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>dmxwhip</title>
<style>
html,body{height:100%;height:100dvh;margin:0;overflow:hidden}
body{display:flex;flex-direction:column;box-sizing:border-box;padding:10px 12px;font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:28rem;margin:0 auto}
h1{font-size:1.15rem;margin:0 0 4px;flex:0 0 auto}
.muted{color:#9aa;font-size:.8rem;margin:0 0 8px;line-height:1.3;flex:0 0 auto}
.tabs{display:flex;gap:8px;flex:0 0 auto;margin:0 0 8px}
.tabs button{margin:0;flex:1}
.tabs button.on{background:#2a6}
#viewNet,#viewPlay{flex:1 1 auto;min-height:0;display:none;flex-direction:column}
#viewNet.on,#viewPlay.on{display:flex}
#scan{flex:0 0 auto}
#list,#plist{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;border:1px solid #333;border-radius:8px;margin:8px 0;padding:4px;background:#161616}
.net,.playrow{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;margin:4px;background:#1c1c1c;border:1px solid #333;border-radius:8px;cursor:pointer}
.net.sel,.playrow.sel{border-color:#6cf}
.form,#ledrow,#liverow,#playopts{flex:0 0 auto}
label{display:block;margin:6px 0 2px;font-size:.8rem;color:#bbb}
input,button,select{width:100%;box-sizing:border-box;padding:8px 10px;font-size:1rem;border-radius:8px;border:1px solid #333;background:#1c1c1c;color:#eee}
.brirow{display:flex;gap:8px;align-items:center}
.brirow input[type=range]{flex:1 1 auto;padding:8px 0;min-width:0}
#brinum{width:4.6rem;flex:0 0 4.6rem;padding:8px 6px;text-align:right}
button{background:#2a6;border:0;margin:8px 0 0;font-weight:600}
button.sec{background:#333}
#savedrow{display:none;margin-top:6px}
#briwarn{display:none;color:#fa6;font-size:.8rem;margin:4px 0 0;line-height:1.3}
#briwarn.on{display:block}
#fileopts,#folderopts,#foldernrow{display:none}
#fileopts.on,#folderopts.on,#foldernrow.on{display:block}
#status{flex:0 0 auto;margin:8px 0 4px;min-height:2.1em;line-height:1.35;font-size:.9rem}
#sd,#ver,#playnow{flex:0 0 auto;color:#9aa;font-size:.75rem;margin:0 0 4px}
.ok{color:#8d8}
.err{color:#f88}
</style>
</head>
<body>
<h1>dmxwhip</h1>
<div class="tabs">
<button class="on" id="tabNet" type="button">Network</button>
<button class="sec" id="tabPlay" type="button">Playback</button>
</div>
<div id="viewNet" class="on">
<p class="muted">2.4 GHz only. If this sheet closes, rejoin <b>dmxwhip</b> or open http://4.3.2.1</p>
<button class="sec" id="scan" type="button">Scan networks</button>
<div id="list"></div>
<div class="form">
<label for="ssid">Network name</label>
<input id="ssid" maxlength="32" placeholder="SSID or hidden network" autocomplete="off">
<label for="pass">Password</label>
<input id="pass" type="password" maxlength="63" placeholder="Leave empty if open" autocomplete="off">
<button id="go" type="button">Connect</button>
<div id="savedrow">
<p class="muted" id="savedlab"></p>
<button class="sec" id="forget" type="button">Forget saved network</button>
</div>
</div>
<div id="ledrow">
<label for="bri">Max brightness <b id="brival">10</b></label>
<div class="brirow">
<input id="bri" type="range" min="0" max="255" value="10">
<input id="brinum" type="number" min="0" max="255" value="10" inputmode="numeric">
</div>
<p id="briwarn">This 8×8 can overheat above 64.</p>
</div>
<div id="liverow">
<label for="proto">Protocol</label>
<select id="proto">
<option value="auto">Auto</option>
<option value="artnet">Art-Net</option>
<option value="sacn">sACN</option>
</select>
<label for="fps">Show FPS</label>
<select id="fps">
<option value="20">20</option>
<option value="30">30</option>
<option value="40" selected>40</option>
<option value="60">60</option>
</select>
<label for="buf">Buffer</label>
<select id="buf">
<option value="0">0 latest</option>
<option value="1">1 frame</option>
<option value="2">2 frames</option>
<option value="3">3 frames</option>
</select>
</div>
<p id="status"></p>
</div>
<div id="viewPlay">
<p class="muted">Idle plays SD. Live Art-Net/sACN preempts.</p>
<p id="sd">SD: …</p>
<p id="playnow"></p>
<div id="plist"></div>
<div id="playopts">
<div id="fileopts">
<label for="fileloop">Loop</label>
<select id="fileloop">
<option value="one">This file</option>
<option value="all">All in this folder</option>
</select>
</div>
<div id="folderopts">
<label for="folderrep">Repeat</label>
<select id="folderrep">
<option value="forever">Forever</option>
<option value="count">Set times</option>
</select>
<div id="foldernrow">
<label for="foldern">Times</label>
<input id="foldern" type="number" min="1" max="99" value="1" inputmode="numeric">
</div>
</div>
</div>
</div>
<p id="ver">dmxwhip</p>
<script>
const list=document.getElementById('list');
const plist=document.getElementById('plist');
const ssidEl=document.getElementById('ssid');
const passEl=document.getElementById('pass');
const statusEl=document.getElementById('status');
const savedRow=document.getElementById('savedrow');
const savedLab=document.getElementById('savedlab');
const verEl=document.getElementById('ver');
const briEl=document.getElementById('bri');
const brinum=document.getElementById('brinum');
const brival=document.getElementById('brival');
const briwarn=document.getElementById('briwarn');
const sdEl=document.getElementById('sd');
const playnowEl=document.getElementById('playnow');
const protoEl=document.getElementById('proto');
const fpsEl=document.getElementById('fps');
const bufEl=document.getElementById('buf');
const fileloopEl=document.getElementById('fileloop');
const folderrepEl=document.getElementById('folderrep');
const foldernEl=document.getElementById('foldern');
const fileopts=document.getElementById('fileopts');
const folderopts=document.getElementById('folderopts');
const foldernrow=document.getElementById('foldernrow');
const viewNet=document.getElementById('viewNet');
const viewPlay=document.getElementById('viewPlay');
const tabNet=document.getElementById('tabNet');
const tabPlay=document.getElementById('tabPlay');
let pollTimer=0;
let briTimer=0;
let liveTimer=0;
let playTimer=0;
let briDirty=false;
let liveDirty=false;
let playDirty=false;
let scanning=false;
let playSrc='root';
let playPath='/';
let playListKey='';
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t;}
function dropHint(){setStatus('Page dropped. Rejoin dmxwhip and open http://4.3.2.1','err');}
function showTab(name){
  const net=name==='net';
  viewNet.className=net?'on':'';
  viewPlay.className=net?'':'on';
  tabNet.className=net?'on':'sec';
  tabPlay.className=net?'sec':'on';
}
function showBri(v){
  briEl.value=v;
  brinum.value=v;
  brival.textContent=v;
  briwarn.className=v>64?'on':'';
}
function applySd(s){
  const sd=s.sd;
  if(!sd||!sd.ok){sdEl.textContent='SD: not mounted';return;}
  sdEl.textContent='SD: '+sd.type+' '+sd.size_mb+' MB  used '+sd.used_mb+'  free '+sd.free_mb;
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
    const on=el.dataset.src===playSrc&&(playSrc==='root'||el.dataset.path===playPath);
    el.className='playrow'+(on?' sel':'');
  }
}
function renderPlayList(p){
  const files=p.files||[];
  const dirs=p.dirs||[];
  const key=(p.src||'')+'|'+(p.path||'')+'|'+files.join('\n')+'|'+dirs.join('\n');
  if(key===playListKey){markPlaySel();return;}
  playListKey=key;
  plist.innerHTML='';
  function row(src,path,label,kind){
    const d=document.createElement('div');
    d.className='playrow';
    d.dataset.src=src;
    d.dataset.path=path;
    d.innerHTML='<span>'+escapeHtml(label)+'</span><span class="muted">'+kind+'</span>';
    d.onclick=()=>{
      playSrc=src;
      playPath=path;
      markPlaySel();
      showPlayOpts();
      schedulePlay();
    };
    plist.appendChild(d);
  }
  row('root','/','All .dmx in /','default');
  files.forEach(f=>row('file',f,f,'.dmx'));
  dirs.forEach(d=>row('folder',d,d,'dir'));
  if(!files.length&&!dirs.length){
    const e=document.createElement('p');
    e.className='muted';
    e.textContent='No .dmx files.';
    plist.appendChild(e);
  }
  markPlaySel();
}
function applyPlay(s){
  const p=s.play;
  if(!p){playnowEl.textContent='';return;}
  if(!playDirty){
    playSrc=p.src||'root';
    playPath=p.path||'/';
    if(p.file_loop) fileloopEl.value=p.file_loop;
    if(p.folder_rep) folderrepEl.value=p.folder_rep;
    if(typeof p.n==='number') foldernEl.value=String(p.n);
  }
  showPlayOpts();
  renderPlayList(p);
  playnowEl.textContent=p.now?'Now: '+p.now:'Now: stopped';
}
function applyLive(s){
  if(liveDirty) return;
  if(s.proto) protoEl.value=s.proto;
  if(typeof s.fps==='number') fpsEl.value=String(s.fps);
  if(typeof s.buf==='number') bufEl.value=String(s.buf);
}
function applyMeta(s){
  if(s.ver) verEl.textContent='dmxwhip v'+s.ver;
  if(s.saved){savedRow.style.display='block';savedLab.textContent='Saved: '+s.saved+' (connects at boot)';}
  else {savedRow.style.display='none';savedLab.textContent='';}
  if(typeof s.bri==='number'&&!briDirty) showBri(s.bri);
  applyLive(s);
  applySd(s);
  applyPlay(s);
}
async function jget(url){
  const r=await fetch(url,{cache:'no-store'});
  if(!r.ok) throw new Error('http');
  return r.json();
}
function renderNets(nets){
  list.innerHTML='';
  (nets||[]).forEach(n=>{
    const d=document.createElement('div');
    d.className='net';
    d.innerHTML='<span>'+escapeHtml(n.ssid)+'</span><span class="muted">'+(n.secure?'lock ':'open ')+n.rssi+' dBm</span>';
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
  applyMeta(s);
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
async function poll(){
  try{
    const s=await jget('/status');
    const fast=showStatus(s);
    pollTimer=setTimeout(poll,fast?500:1000);
  }catch(e){dropHint();}
}
async function connect(){
  const ssid=ssidEl.value.trim();
  const password=passEl.value;
  if(!ssid){setStatus('Enter a network name.','err');return;}
  try{
    const body=new URLSearchParams({ssid,password});
    const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
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
  const body=new URLSearchParams({v:String(v)});
  fetch('/brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(()=>{briDirty=false;})
    .catch(dropHint);
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
  if(t==='') return null;
  if(!/^\d+$/.test(t)) return null;
  return parseInt(t,10);
}
briEl.oninput=()=>scheduleBri(+briEl.value);
brinum.oninput=()=>{
  const n=parseBriNum();
  if(n===null) return;
  scheduleBri(n);
};
brinum.onchange=()=>{
  const n=parseBriNum();
  if(n===null){showBri(+briEl.value);return;}
  scheduleBri(n);
};
function postLive(){
  const body=new URLSearchParams({proto:protoEl.value,fps:fpsEl.value,buf:bufEl.value});
  fetch('/live',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(async r=>{
      if(!r.ok) throw new Error('http');
      const s=await r.json();
      liveDirty=false;
      applyMeta(s);
    })
    .catch(dropHint);
}
function scheduleLive(){
  liveDirty=true;
  clearTimeout(liveTimer);
  liveTimer=setTimeout(postLive,300);
}
function parsePlayN(){
  const t=foldernEl.value.trim();
  if(!/^\d+$/.test(t)) return 1;
  return Math.max(1,Math.min(99,parseInt(t,10)));
}
function postPlay(){
  const n=parsePlayN();
  foldernEl.value=String(n);
  const body=new URLSearchParams({
    src:playSrc,path:playPath,file_loop:fileloopEl.value,folder_rep:folderrepEl.value,n:String(n)
  });
  fetch('/play',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(async r=>{
      if(!r.ok) throw new Error('http');
      const s=await r.json();
      playDirty=false;
      applyMeta(s);
    })
    .catch(dropHint);
}
function schedulePlay(){
  playDirty=true;
  clearTimeout(playTimer);
  playTimer=setTimeout(postPlay,300);
}
protoEl.onchange=scheduleLive;
fpsEl.onchange=scheduleLive;
bufEl.onchange=scheduleLive;
fileloopEl.onchange=schedulePlay;
folderrepEl.onchange=()=>{showPlayOpts();schedulePlay();};
foldernEl.onchange=schedulePlay;
foldernEl.oninput=schedulePlay;
tabNet.onclick=()=>showTab('net');
tabPlay.onclick=()=>showTab('play');
document.getElementById('scan').onclick=()=>scan(true);
document.getElementById('go').onclick=connect;
document.getElementById('forget').onclick=forget;
(async()=>{
  try{
    const s=await jget('/status');
    applyMeta(s);
    const connecting=showStatus(s)&&s.state==='connecting';
    poll();
    if(!connecting) await scan(true);
  }catch(e){dropHint();}
})();
</script>
</body>
</html>
)WIFIHTML";
