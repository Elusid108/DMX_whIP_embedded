#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>dmxwhip</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:28rem}
h1{font-size:1.25rem;margin:0 0 8px}
.muted{color:#9aa;font-size:.9rem;margin:0 0 16px;line-height:1.4}
.net{display:flex;justify-content:space-between;gap:8px;padding:10px 12px;margin:6px 0;background:#1c1c1c;border:1px solid #333;border-radius:8px;cursor:pointer}
.net.sel{border-color:#6cf}
label{display:block;margin:12px 0 4px;font-size:.85rem;color:#bbb}
input,button{width:100%;box-sizing:border-box;padding:10px 12px;font-size:1rem;border-radius:8px;border:1px solid #333;background:#1c1c1c;color:#eee}
button{background:#2a6;border:0;margin:10px 0 0;font-weight:600}
button.sec{background:#333}
#status{margin-top:14px;min-height:2.4em;line-height:1.4}
.ok{color:#8d8}
.err{color:#f88}
</style>
</head>
<body>
<h1>dmxwhip</h1>
<p class="muted">Join this device to a 2.4 GHz network. If this page drops, reconnect to <b>dmxwhip</b> and open http://192.168.4.1</p>
<button class="sec" id="scan" type="button">Scan networks</button>
<div id="list"></div>
<label for="ssid">Network name</label>
<input id="ssid" maxlength="32" placeholder="SSID or hidden network" autocomplete="off">
<label for="pass">Password</label>
<input id="pass" type="password" maxlength="63" placeholder="Leave empty if open" autocomplete="off">
<button id="go" type="button">Connect</button>
<p id="status"></p>
<script>
const list=document.getElementById('list');
const ssidEl=document.getElementById('ssid');
const passEl=document.getElementById('pass');
const statusEl=document.getElementById('status');
let pollTimer=0;
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t;}
function dropHint(){setStatus('Page dropped. Rejoin dmxwhip and open http://192.168.4.1','err');}
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
      [...list.children].forEach(c=>c.classList.remove('sel'));
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
    if(!r.ok){setStatus(s.error||'Connect rejected','err');return;}
    showStatus(s);
    clearTimeout(pollTimer);
    poll();
  }catch(e){dropHint();}
}
document.getElementById('scan').onclick=()=>scan(true);
document.getElementById('go').onclick=connect;
(async()=>{
  try{
    const s=await jget('/status');
    if(showStatus(s)&&s.state==='connecting') poll();
    else if(s.state!=='connected') await scan(false);
  }catch(e){dropHint();}
})();
</script>
</body>
</html>
)WIFIHTML";
