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
#scan{flex:0 0 auto}
#list{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;border:1px solid #333;border-radius:8px;margin:8px 0;padding:4px;background:#161616}
.net{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;margin:4px;background:#1c1c1c;border:1px solid #333;border-radius:8px;cursor:pointer}
.net.sel{border-color:#6cf}
.form{flex:0 0 auto}
label{display:block;margin:6px 0 2px;font-size:.8rem;color:#bbb}
input,button{width:100%;box-sizing:border-box;padding:8px 10px;font-size:1rem;border-radius:8px;border:1px solid #333;background:#1c1c1c;color:#eee}
button{background:#2a6;border:0;margin:8px 0 0;font-weight:600}
button.sec{background:#333}
#savedrow{display:none;margin-top:6px}
#status{flex:0 0 auto;margin:8px 0 4px;min-height:2.1em;line-height:1.35;font-size:.9rem}
#ver{flex:0 0 auto;color:#9aa;font-size:.75rem}
.ok{color:#8d8}
.err{color:#f88}
</style>
</head>
<body>
<h1>dmxwhip</h1>
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
<p id="status"></p>
<p id="ver">dmxwhip</p>
<script>
const list=document.getElementById('list');
const ssidEl=document.getElementById('ssid');
const passEl=document.getElementById('pass');
const statusEl=document.getElementById('status');
const savedRow=document.getElementById('savedrow');
const savedLab=document.getElementById('savedlab');
const verEl=document.getElementById('ver');
let pollTimer=0;
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t;}
function dropHint(){setStatus('Page dropped. Rejoin dmxwhip and open http://4.3.2.1','err');}
function applyMeta(s){
  if(s.ver) verEl.textContent='dmxwhip v'+s.ver;
  if(s.saved){savedRow.style.display='block';savedLab.textContent='Saved: '+s.saved+' (connects at boot)';}
  else {savedRow.style.display='none';savedLab.textContent='';}
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
  setStatus('Scanning…');
  try{
    await jget('/scan'+(force?'?start=1':''));
    for(let i=0;i<25;i++){
      const data=await jget('/scan');
      if(data.state==='scanning'){await new Promise(r=>setTimeout(r,400));continue;}
      if(data.state==='connecting'){setStatus('Connect in progress…');return;}
      renderNets(data.networks||[]);
      if((data.networks||[]).length) setStatus('Select a network, then Connect.');
      return;
    }
    setStatus('Scan timed out. Try again.','err');
  }catch(e){dropHint();}
}
function showStatus(s){
  applyMeta(s);
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
    if(showStatus(s)) pollTimer=setTimeout(poll,500);
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
document.getElementById('scan').onclick=()=>scan(true);
document.getElementById('go').onclick=connect;
document.getElementById('forget').onclick=forget;
(async()=>{
  try{
    const s=await jget('/status');
    applyMeta(s);
    if(showStatus(s)&&s.state==='connecting') poll();
    else if(s.state!=='connected') await scan(false);
  }catch(e){dropHint();}
})();
</script>
</body>
</html>
)WIFIHTML";
