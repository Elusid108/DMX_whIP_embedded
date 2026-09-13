#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>dmxwhip</title>
<style>
:root{--bg:#09090b;--chrome:#18181b;--border:#27272a;--text:#e4e4e7;--muted:#71717a;--accent:#22d3ee}
html,body{height:100%;height:100dvh;margin:0;overflow:hidden}
body{display:flex;flex-direction:column;box-sizing:border-box;padding:10px 12px;font-family:system-ui,sans-serif;background:var(--bg);color:var(--text);max-width:28rem;margin:0 auto}
.strip{flex:0 0 auto;padding:0 0 8px;margin:0 0 8px;border-bottom:1px solid var(--border)}
.namerow{display:flex;gap:6px;align-items:center}
.namerow input{flex:1 1 auto;min-width:0}
.readout{font-family:ui-monospace,monospace;font-variant-numeric:tabular-nums;font-size:11px;color:var(--muted);margin:6px 0 0;line-height:1.35;word-break:break-word}
.tabs{display:flex;gap:0;flex:0 0 auto;border-bottom:1px solid var(--border);margin:0 0 8px}
.tabs button{flex:1;margin:0;border:0;border-bottom:2px solid transparent;border-radius:0;background:transparent;color:var(--muted);font-weight:500}
.tabs button.on{color:var(--accent);border-bottom-color:var(--accent)}
#viewPlay,#viewSetup{flex:1 1 auto;min-height:0;display:none;flex-direction:column}
#viewPlay.on,#viewSetup.on{display:flex}
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
#savedrow,#renrow,#fileopts,#folderopts,#foldernrow,#briwarn,#note{display:none}
#savedrow.on,#renrow.on,#fileopts.on,#folderopts.on,#foldernrow.on,#briwarn.on,#note.on{display:block}
#briwarn{color:#f59e0b;font-size:.8rem;margin:4px 0 0}
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
<div class="namerow">
<input id="name" maxlength="63" autocomplete="off">
<button id="saveName" type="button">Save</button>
<button class="pri" id="identify" type="button">Identify</button>
</div>
<p class="readout" id="meta"></p>
<p id="note"></p>
</div>
<div class="tabs">
<button class="on" id="tabPlay" type="button">Playback</button>
<button id="tabSetup" type="button">Setup</button>
</div>
<div id="viewPlay" class="on">
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
<div class="mod lab">Output</div>
<label class="lab" for="bri">Brightness</label>
<div class="brirow">
<input id="bri" type="range" min="0" max="255" value="10">
<input id="brinum" type="number" min="0" max="255" value="10" inputmode="numeric">
</div>
<p id="briwarn">This 8×8 can overheat above 64.</p>
<div class="grid3">
<div><label class="lab" for="proto">Protocol</label>
<select id="proto"><option value="auto">Auto</option><option value="artnet">Art-Net</option><option value="sacn">sACN</option></select></div>
<div><label class="lab" for="fps">FPS</label>
<select id="fps"><option value="20">20</option><option value="30">30</option><option value="40" selected>40</option><option value="60">60</option></select></div>
<div><label class="lab" for="buf">Buffer</label>
<select id="buf"><option value="0">0 latest</option><option value="1">1 frame</option><option value="2">2 frames</option><option value="3">3 frames</option></select></div>
</div>
<label class="lab" for="park">Park portal while live</label>
<select id="park"><option value="yes" selected>Yes</option><option value="no">No</option></select>
</div>
<p id="ver" class="readout">dmxwhip</p>
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
const metaEl=document.getElementById('meta');
const nameEl=document.getElementById('name');
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
const viewPlay=document.getElementById('viewPlay');
const viewSetup=document.getElementById('viewSetup');
const tabPlay=document.getElementById('tabPlay');
const tabSetup=document.getElementById('tabSetup');
const idBtn=document.getElementById('identify');
let pollTimer=0,briTimer=0,liveTimer=0;
let briDirty=false,liveDirty=false,playDirty=false,nameDirty=false,scanning=false;
let playSrc='root',playPath='/',playListKey='',lastFiles=[];
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t||'';}
function setNote(t,cls){noteEl.className=t?(cls||'err')+' on':'';noteEl.textContent=t||'';}
function dropHint(){setNote('Page dropped. Rejoin dmxwhip and open http://4.3.2.1','err');}
function showTab(name){
  const play=name==='play';
  viewPlay.className=play?'on':'';
  viewSetup.className=play?'':'on';
  tabPlay.className=play?'on':'';
  tabSetup.className=play?'':'on';
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
function applyMeta(s){
  if(s.ver) verEl.textContent='dmxwhip v'+s.ver;
  if(!nameDirty&&s.name) nameEl.value=s.name;
  const sd=s.sd;
  const sdLine=!sd?'no SD':!sd.ok?'SD not mounted':'SD '+sd.used_mb+'/'+sd.size_mb+' MB';
  const now=s.play&&s.play.now?displayName(s.play.now):'stopped';
  const live=!!s.live;
  ['prev','play','stop','next','rename'].forEach(id=>{const el=document.getElementById(id);if(el) el.disabled=live;});
  idBtn.disabled=live;
  metaEl.textContent=[s.ip?('STA '+s.ip):null,s.ver?('fw '+s.ver):null,sdLine,live?'live':'idle','now '+now].filter(Boolean).join(' · ');
  if(s.saved){savedRow.className='on';savedLab.textContent='saved '+s.saved+' (connects at boot)'+(s.ip?(' · STA '+s.ip):'');}
  else {savedRow.className='';savedLab.textContent='';}
  if(typeof s.bri==='number'&&!briDirty) showBri(s.bri);
  applyLive(s);
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
    setNote('');
    const fast=showStatus(s);
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
protoEl.onchange=scheduleLive;
fpsEl.onchange=scheduleLive;
bufEl.onchange=scheduleLive;
parkEl.onchange=scheduleLive;
folderrepEl.onchange=showPlayOpts;
tabPlay.onclick=()=>showTab('play');
tabSetup.onclick=()=>showTab('setup');
document.getElementById('scan').onclick=()=>scan(true);
document.getElementById('go').onclick=connect;
document.getElementById('forget').onclick=forget;
document.getElementById('play').onclick=()=>postPlay();
document.getElementById('stop').onclick=()=>postForm('/play',{src:'stop'}).then(async r=>{if(!r.ok) throw new Error('http');applyMeta(await r.json());}).catch(dropHint);
document.getElementById('prev').onclick=()=>{const t=adjacent(-1);if(t){playDirty=true;playSrc='file';playPath=t;markPlaySel();showPlayOpts();postPlay('file',t);}};
document.getElementById('next').onclick=()=>{const t=adjacent(1);if(t){playDirty=true;playSrc='file';playPath=t;markPlaySel();showPlayOpts();postPlay('file',t);}};
nameEl.oninput=()=>{nameDirty=true;};
document.getElementById('saveName').onclick=()=>{
  const long=nameEl.value.trim();
  if(!long){setNote('Enter a device name','err');return;}
  postForm('/name',{long}).then(async r=>{
    if(!r.ok) throw new Error('http');
    nameDirty=false;
    setNote('');
    applyMeta(await r.json());
  }).catch(dropHint);
};
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
