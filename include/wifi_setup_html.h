#pragma once

#include <pgmspace.h>

static const char kWifiSetupHtml[] PROGMEM = R"WIFIHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>dmxwhip v0.50.0</title>
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
.livehead{display:flex;justify-content:center;align-items:center;gap:8px;margin:0 0 8px}
#toStream{display:none;width:auto;margin:0;padding:4px 12px;font-size:.8rem}
#toStream.on{display:inline-block}
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
.patchbar{justify-content:space-between;align-items:center}
.patchbar #mapAdd{margin-left:auto}
.patchops button.icon{padding:4px 7px;font-size:1rem;line-height:1}
.tabs{display:flex;gap:0;flex:0 0 auto;border-bottom:1px solid var(--border);margin:0 0 8px}
.tabs button{flex:1;margin:0;padding:8px 4px;border:0;border-bottom:2px solid transparent;border-radius:0;background:transparent;color:var(--muted);font-weight:500;font-size:.8rem}
.tabs button.on{color:var(--accent);border-bottom-color:var(--accent)}
#viewLive,#viewPlay,#viewPixels,#viewSetup{flex:1 1 auto;min-height:0;display:none;flex-direction:column}
#viewLive.on,#viewPlay.on,#viewPixels.on,#viewSetup.on{display:flex}
#viewPixels,#viewSetup{overflow-y:auto;-webkit-overflow-scrolling:touch}
#viewLive{overflow:hidden}
.lab{display:block;margin:8px 0 2px;font-size:10px;font-weight:500;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.mod{margin-top:10px;padding-top:10px;border-top:1px solid var(--border)}
#list,#plist{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;border:1px solid var(--border);border-radius:6px;margin:6px 0;padding:3px;background:var(--bg)}
#list{flex:1 1 8rem;min-height:8rem}
.net,.playrow{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:8px 10px;margin:3px;background:var(--chrome);border:1px solid var(--border);border-radius:6px;cursor:pointer;user-select:none;-webkit-user-select:none}
.net.sel,.playrow.sel{border-color:#22d3ee66;box-shadow:inset 2px 0 0 var(--accent)}
.net.now,.playrow.now{border-color:#86efac66}
.playrow .playname{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.playrow .playfile{flex:0 1 auto;max-width:42%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:ui-monospace,monospace;font-size:11px;color:var(--muted)}
.playrow .playboot,.playrow .playrole{flex:0 0 auto;font-size:10px;letter-spacing:.04em;text-transform:uppercase;color:var(--accent)}
.playrow .playmark{flex:0 0 auto;font-size:10px;letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
.playrow .chev{width:1.15rem;flex:0 0 auto;padding:0;margin:0;border:0;background:transparent;color:var(--muted);font-size:.7rem;line-height:1}
.playrow input{flex:1;min-width:0;width:auto;padding:2px 6px;margin:0;font-size:.85rem}
.transport{justify-content:center;align-items:center}
.tbtn{width:2.5rem;height:2.5rem;padding:6px;margin:0;display:inline-flex;align-items:center;justify-content:center}
.tbtn.on{border-color:var(--accent);color:var(--accent)}
.tbtn svg{width:18px;height:18px;fill:currentColor;display:block}
.row{display:flex;flex-wrap:wrap;gap:6px;flex:0 0 auto}
.row button{width:auto;margin:0;flex:0 0 auto}
input,button,select{width:100%;box-sizing:border-box;padding:8px 10px;font-size:1rem;border-radius:6px;border:1px solid var(--border);background:var(--chrome);color:var(--text)}
.brirow{display:flex;gap:8px;align-items:center}
.brirow input[type=range]{flex:1 1 auto;padding:8px 0;min-width:0;accent-color:var(--accent)}
#brinum{width:4.6rem;flex:0 0 4.6rem;padding:8px 6px;text-align:right;font-family:ui-monospace,monospace}
.passwrap{position:relative}
.passwrap input{padding-right:2.75rem}
#passEye{position:absolute;right:2px;top:50%;transform:translateY(-50%);width:auto;min-width:2.2rem;margin:0;padding:6px;border:0;background:transparent;color:var(--muted);line-height:0}
#passEye svg{display:block;width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2}
button{background:var(--chrome);border:1px solid var(--border);margin:6px 0 0;font-weight:600;color:var(--text)}
button.pri{background:var(--accent);border-color:var(--accent);color:var(--bg)}
#savedrow,#fileopts,#folderopts,#foldernrow,#note{display:none}
#savedrow.on,#fileopts.on,#folderopts.on,#foldernrow.on,#note.on{display:block}
.clkrow,.briwarn{display:none}
.clkrow.on,.briwarn.on{display:block}
.tog{display:flex;align-items:center;gap:8px;margin:8px 0 0}
.tog input{width:auto;margin:0}
.briwarn{color:#f59e0b;font-size:.8rem;margin:4px 0 0}
.testrow{display:flex;gap:6px;padding:0 10px 8px}
.testrow button{flex:1;margin:0;padding:8px 4px;font-size:.75rem}
.testrow button.on{background:var(--accent);border-color:var(--accent);color:var(--bg)}
.testrow button:disabled{opacity:.4}
#patchList{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch}
.patch{border:1px solid var(--border);border-radius:8px;margin:0 0 8px;background:var(--chrome)}
.patch.child{margin-left:14px;border-left:2px solid var(--accent)}
.patchhead{display:flex;align-items:center;gap:6px;padding:8px 8px 8px 10px;cursor:pointer}
.patchhead b{flex:1;min-width:0;font-size:.8rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.patchops{display:flex;gap:4px;flex:0 0 auto}
.patchops button{width:auto;margin:0;padding:4px 8px;font-size:.75rem}
.patchbody{display:none;padding:0 10px 10px}
.patch.open .patchbody{display:block}
.patchgrid{display:grid;grid-template-columns:1fr 1fr;gap:0 8px}
.patchfield{min-width:0}
.patchfield .lab{margin-top:6px}
.patchgrid .span2{grid-column:1/-1}
.patchtogs{display:flex;align-items:center;gap:12px;min-height:2.5rem}
.patchtogs .tog{margin:0}
#note{margin:6px 0 0;font-size:.85rem}
#status,#ver{flex:0 0 auto;margin:6px 0 0;font-size:.85rem;color:var(--muted);min-height:1.2em}
.ok{color:#86efac}
.err{color:#f87171}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}
.hint{color:var(--muted);font-size:.75rem;margin:0 0 6px;line-height:1.3}
.seg2{display:flex;margin:0 0 8px;border:1px solid var(--border);border-radius:6px;overflow:hidden;flex:0 0 auto}
.seg2 button{flex:1;width:auto;margin:0;border:0;border-radius:0;padding:6px;font-size:.8rem;background:var(--chrome);color:var(--muted)}
.seg2 button.on{background:var(--accent);color:var(--bg)}
#pxMainBox{display:flex;flex-direction:column;flex:1 1 auto;min-height:0}
#pxMainBox.off,#fxBox.off{display:none}
#fxBox{flex:0 0 auto}
.fxmeta{font:11px/1.3 ui-monospace,monospace;color:var(--muted);word-break:break-word}
.fxbar{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0}
.fxbar button,.fxbar select{width:auto;margin:0;padding:6px 10px;font-size:.8rem}
.fxsub{display:grid;grid-template-columns:1fr auto;gap:4px 6px;align-items:center;border:1px solid var(--border);border-radius:6px;padding:6px 8px;margin:0 0 6px;background:var(--chrome)}
.fxsub input{padding:4px 6px;font-size:.85rem;margin:0}
.fxsub .fxmeta{grid-column:1/-1}
.fxseg{border:1px solid var(--border);border-radius:6px;margin:0 0 6px;background:var(--chrome)}
.fxseghead{display:flex;align-items:center;gap:6px;padding:6px 8px}
.fxseghead b{flex:1;min-width:0;font-size:.75rem;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fxpx{display:grid;grid-template-columns:2.6em 1fr;gap:2px 6px;align-items:center;padding:4px 8px;border-top:1px solid var(--border);cursor:pointer}
.fxpx .fxmeta{grid-column:2}
.fxpx.sel{background:#22d3ee1f;box-shadow:inset 2px 0 0 var(--accent)}
.fxpx input{padding:3px 6px;font-size:.85rem;margin:0}
.fxidx{font:11px ui-monospace,monospace;color:var(--muted);text-align:right}
.fxerr{color:#f87171;font-size:.8rem;margin:4px 0}
.fxmapw{max-height:18rem;overflow:auto;border:1px solid var(--border);border-radius:6px;margin-top:4px}
.fxmap{width:100%;border-collapse:collapse;font-size:.8rem}
.fxmap th,.fxmap td{padding:3px 6px;border-bottom:1px solid var(--border);text-align:left;vertical-align:top}
.fxmap th{color:var(--muted);font-weight:500;position:sticky;top:0;background:var(--chrome)}
.fxmap td:first-child{white-space:nowrap;font-variant-numeric:tabular-nums}
.fxmap td:last-child{color:var(--muted)}
.setcol{min-width:0}
.setcol #list{max-height:40vh}
@media (min-width:768px){
body{max-width:60rem;padding:14px 20px}
.dash{grid-template-columns:repeat(4,minmax(0,1fr));grid-template-rows:minmax(0,13rem);align-content:start}
#viewPlay.on{display:grid;grid-template-columns:minmax(0,3fr) minmax(0,2fr);grid-template-rows:auto repeat(6,auto) 1fr;column-gap:16px}
#viewPlay>.hint{grid-column:1/-1}
#plist{grid-column:1;grid-row:2/-1;margin-top:0}
#viewPlay>:not(.hint):not(#plist){grid-column:2}
#viewSetup.on{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);column-gap:24px;align-content:start}
#viewSetup>.hint{grid-column:1/-1}
.setcol>.mod:first-child{border-top:0;margin-top:0;padding-top:0}
.fxsub{grid-template-columns:minmax(0,1fr) auto}
}
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
<div class="tabs">
<button class="on" id="tabLive" type="button">Live</button>
<button id="tabPlay" type="button">Playback</button>
<button id="tabPixels" type="button">Patch</button>
<button id="tabSetup" type="button">Setup</button>
</div>
<div id="viewLive" class="on">
<div class="livehead"><div class="mode" id="mode"><i></i><span id="modeLab">idle</span></div><button id="toStream" type="button">Stream</button></div>
<div class="dash">
<div class="tile">
<div class="clab">Radio</div>
<div class="tval" id="lrSsid">—</div>
<p class="tmono" id="lrSta">—</p>
<div class="sig">
<div class="bars" id="lrBars" data-n="0"><i></i><i></i><i></i><i></i></div>
<span class="tmono" id="lrRssi">—</span>
<span class="tmono" id="lrLink">—</span>
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
<span class="pip" id="lpPause"><i></i>paused</span>
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
<p class="hint">Idle plays the startup clip, if one is set. A live stream takes over. Play asks before overriding a stream.</p>
<div id="plist"></div>
<p class="readout" id="playnow"></p>
<div class="row transport">
<button id="prev" class="tbtn" type="button" aria-label="Previous"><svg viewBox="0 0 24 24"><path d="M7 6h2v12H7zm3 6 8 6V6z"/></svg></button>
<button id="play" class="tbtn pri" type="button" aria-label="Play"><svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg></button>
<button id="pause" class="tbtn" type="button" aria-label="Pause"><svg viewBox="0 0 24 24"><path d="M6 5h4v14H6zm8 0h4v14h-4z"/></svg></button>
<button id="stop" class="tbtn" type="button" aria-label="Stop"><svg viewBox="0 0 24 24"><path d="M6 6h12v12H6z"/></svg></button>
<button id="next" class="tbtn" type="button" aria-label="Next"><svg viewBox="0 0 24 24"><path d="M15 6h2v12h-2zM6 6l8 6-8 6z"/></svg></button>
<button id="del" class="tbtn" type="button" aria-label="Delete"><svg viewBox="0 0 24 24"><path d="M9 3h6l1 2h5v2H3V5h5zm1 6h2v10h-2zm4 0h2v10h-2z"/></svg></button>
<button id="strm" class="tbtn" type="button" aria-label="Stream to the group" title="Stream: play this full show here and send every node its part live (no SD needed on them)"><svg viewBox="0 0 24 24"><path d="M12 10a2 2 0 1 1 0 4 2 2 0 0 1 0-4zM7.8 7.8l1.4 1.4a4 4 0 0 0 0 5.6l-1.4 1.4a6 6 0 0 1 0-8.4zm8.4 0a6 6 0 0 1 0 8.4l-1.4-1.4a4 4 0 0 0 0-5.6zM5 5l1.4 1.4a8 8 0 0 0 0 11.2L5 19A10 10 0 0 1 5 5zm14 0a10 10 0 0 1 0 14l-1.4-1.4a8 8 0 0 0 0-11.2z"/></svg></button>
<button id="dist" class="tbtn" type="button" aria-label="Distribute to the group" title="Distribute: slice this full show for every node on the network and send each its part"><svg viewBox="0 0 24 24"><path d="M11 3h2v6h-2zM5 13h14v2H5zm-2 4h4v4H3zm7 0h4v4h-4zm7 0h4v4h-4zM11 9h2v4h-2zM4 15h2v2H4zm14 0h2v2h-2zm-7 0h2v2h-2z"/></svg></button>
</div>
<div id="fileopts"><label class="lab" for="fileloop">Loop</label>
<select id="fileloop"><option value="one">This file</option><option value="all">All in this folder</option></select></div>
<div id="folderopts"><label class="lab" for="folderrep">Repeat</label>
<select id="folderrep"><option value="forever">Forever</option><option value="count">Set times</option></select>
<div id="foldernrow"><label class="lab" for="foldern">Times</label>
<input id="foldern" type="number" min="1" max="99" value="1" inputmode="numeric"></div></div>
<p class="readout" id="bootnow"></p>
<div class="row"><button id="setStartup" type="button">Set startup</button><button id="clearStartup" type="button">Clear startup</button></div>
</div>
<div id="viewPixels">
<div class="seg2"><button id="pxTabMain" class="on" type="button">Main patch</button><button id="pxTabAdv" type="button">Advanced</button></div>
<div id="pxMainBox">
<p class="hint">Each row is a fixture. Same data GPIO chains under the parent (top of the group is first on the wire). Save writes the map and applies it. Brightness applies immediately.</p>
<div id="patchList"></div>
<div class="row patchbar">
<button class="pri" id="mapSave" type="button">Save</button>
<button id="mapAdd" type="button">Add Output +</button>
</div>
</div>
<div id="fxBox" class="off">
<p class="hint">Makes this node one console fixture with its own address. Pixels come from the saved main patch. Group them into sub-fixtures; the channel list follows the mode.</p>
<label class="tog"><input id="fxEn" type="checkbox"> Enabled</label>
<label class="lab" for="fxMode">Mode</label>
<select id="fxMode">
<option value="basic">Basic</option>
<option value="dim">Dim + FX</option>
<option value="rgb">RGB + FX</option>
<option value="full">Full</option>
</select>
<p class="hint" id="fxModeHint"></p>
<p class="hint">A channel left at 0 has no effect, so channels your console doesn’t patch are ignored. Dimmers stay open until the console first raises them.</p>
<div class="grid3">
<div><label class="lab" for="fxProto">Protocol</label><select id="fxProto"><option value="artnet">Art-Net</option><option value="sacn">sACN</option></select></div>
<div><label class="lab" for="fxUni">Universe</label><input id="fxUni" type="number" min="0" max="63999" inputmode="numeric"></div>
<div><label class="lab" for="fxCh">Channel</label><input id="fxCh" type="number" min="1" max="512" inputmode="numeric"></div>
</div>
<p class="readout" id="fxFoot"></p>
<p class="fxerr" id="fxErr"></p>
<details open><summary class="lab">Channel map</summary><div class="fxmapw"><table class="fxmap" id="fxHdr"></table></div></details>
<div class="mod lab">Sub-fixtures</div>
<p class="hint" id="fxBasicNote" hidden>Basic mode has no sub-fixtures. Any you group are kept for the other modes.</p>
<div id="fxSubs"></div>
<div class="mod lab">Pixels</div>
<div class="fxbar">
<button id="fxGroup" type="button">Group as new</button>
<select id="fxAddTo" aria-label="Sub-fixture to add to"></select>
<button id="fxAdd" type="button">Add to</button>
<button id="fxUngroup" type="button">Ungroup</button>
<button id="fxLocate" type="button">Locate</button>
<button id="fxClear" type="button">Clear</button>
</div>
<p class="fxmeta" id="fxSel">Tap pixels to select; Shift-tap selects a run.</p>
<div id="fxTree"></div>
<div class="row" style="margin-top:8px">
<button class="pri" id="fxSave" type="button">Save advanced patch</button>
</div>
</div>
</div>
<div id="viewSetup">
<p class="hint" id="setupHint">2.4 GHz only. If this sheet closes, rejoin <b>dmxwhip</b> or open http://4.3.2.1</p>
<div class="setcol">
<div class="lab">Radio</div>
<div class="row">
<button id="scan" type="button">Scan</button>
<button class="pri" id="go" type="button">Connect</button>
<button id="forget" type="button">Forget</button>
</div>
<div id="bandrow" hidden>
<label class="lab" for="band">Band</label>
<select id="band">
<option value="2g" selected>2.4 GHz</option>
<option value="5g">5 GHz</option>
<option value="auto">Auto</option>
</select>
</div>
<div id="list"></div>
<label class="lab" for="ssid">SSID</label>
<input id="ssid" maxlength="32" placeholder="SSID or hidden network" autocomplete="off">
<label class="lab" for="pass">Password</label>
<div class="passwrap">
<input id="pass" type="password" maxlength="63" placeholder="Leave empty if open" autocomplete="off">
<button id="passEye" type="button" aria-label="Show password" title="Show password">
<svg viewBox="0 0 24 24"><ellipse cx="12" cy="12" rx="8" ry="5"/><circle cx="12" cy="12" r="2"/></svg>
</button>
</div>
<div id="savedrow"><p class="readout" id="savedlab"></p></div>
<p id="status"></p>
</div>
<div class="setcol">
<div class="mod lab">Live input</div>
<div class="grid3">
<div><label class="lab" for="fps">FPS</label>
<select id="fps"><option value="20">20</option><option value="30">30</option><option value="40" selected>40</option><option value="60">60</option></select></div>
<div><label class="lab" for="buf">Buffer</label>
<select id="buf"><option value="0">0 latest</option><option value="1">1 frame</option><option value="2">2 frames</option><option value="3">3 frames</option></select></div>
</div>
<label class="lab" for="park">Hide AP if connected</label>
<select id="park"><option value="yes" selected>Yes</option><option value="no">No</option></select>
<label class="lab" for="takeover">Sync takeover</label>
<select id="takeover"><option value="yes" selected>Yes</option><option value="no">No</option></select>
<p class="hint">Yes joins a split clip launched on another node, then returns here. No stays on this show.</p>
<label class="lab" for="unisync">Uni-Sync</label>
<select id="unisync"><option value="no" selected>No</option><option value="yes">Yes</option></select>
<p class="hint">Yes plays a copied clip together when another node launches it. No ignores those copies and does not send them.</p>
<label class="lab" for="loss">No signal</label>
<select id="loss">
<option value="play" selected>Play</option>
<option value="hold">Hold</option>
<option value="black">Black</option>
</select>
<p class="hint">Play resumes the show after the stream stops. Hold keeps the last frame. Black clears the lights.</p>
<div class="row">
<button class="pri" id="setupSave" type="button">Save</button>
</div>
<div class="mod lab">Show network</div>
<label class="lab" for="snRole">Role</label>
<select id="snRole"><option value="standalone">Standalone</option><option value="host">Show Host (runs the Wi-Fi)</option><option value="member">Member (joins the Show Host)</option></select>
<div id="snFields">
<label class="lab" for="snSsid">Show SSID</label>
<input id="snSsid" maxlength="32" autocomplete="off">
<label class="lab" for="snPass">Show password</label>
<input id="snPass" type="password" maxlength="63" placeholder="8+ characters (empty keeps the saved one)" autocomplete="off">
<label class="lab" for="snCh">Channel</label>
<select id="snCh"><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option selected>6</option><option>7</option><option>8</option><option>9</option><option>10</option><option>11</option></select>
</div>
<p class="hint">The Show Host runs its own 2.4 GHz network at http://10.77.0.1 and keeps the show clock; members join it and fall back to the saved network. Saving reboots this node.</p>
<div class="row">
<button id="snSave" type="button">Save &amp; reboot</button>
</div>
<div class="mod lab">Firmware</div>
<p class="readout" id="fwInfo"></p>
<p class="briwarn" id="fwRb">The last update did not start cleanly, so this node went back to the version above.</p>
<label class="lab" for="fwFile">Firmware file (.bin)</label>
<input id="fwFile" type="file" accept=".bin,application/octet-stream">
<div class="sbar" id="fwBarWrap" hidden><i id="fwBar"></i></div>
<div class="row">
<button class="pri" id="fwUp" type="button">Upload &amp; update</button>
</div>
<p class="hint">The node checks the file is for this board, writes it and reboots itself. If the new firmware does not come up cleanly it goes back to this version.</p>
<div class="row">
<button id="fwPeers" type="button">Update other nodes</button>
</div>
<label class="tog"><input id="fwForce" type="checkbox"> Include busy nodes (playing or live)</label>
<p class="hint">Sends this node's firmware to every other node of the same board that runs an older version, one at a time. They reboot themselves.</p>
<div id="fwRows"></div>
</div>
</div>
<p id="ver" class="readout">dmxwhip v0.50.0</p>
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
const playnowEl=document.getElementById('playnow');
const bootnowEl=document.getElementById('bootnow');
const fpsEl=document.getElementById('fps');
const bufEl=document.getElementById('buf');
const parkEl=document.getElementById('park');
const takeoverEl=document.getElementById('takeover');
const unisyncEl=document.getElementById('unisync');
const lossEl=document.getElementById('loss');
const bandEl=document.getElementById('band');
const bandRow=document.getElementById('bandrow');
const setupHint=document.getElementById('setupHint');
const setupSave=document.getElementById('setupSave');
const patchList=document.getElementById('patchList');
const mapAdd=document.getElementById('mapAdd');
const fileloopEl=document.getElementById('fileloop');
const folderrepEl=document.getElementById('folderrep');
const foldernEl=document.getElementById('foldern');
const fileopts=document.getElementById('fileopts');
const folderopts=document.getElementById('folderopts');
const foldernrow=document.getElementById('foldernrow');
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
function defaultSeg(){return {proto:'auto',chip:'ws2812b',data:patchCaps.led_data,clk:0,count:patchCaps.panel_px||64,white:false,cct:false,order:'grb',uni:0,ch:1,bri:10};}
let patchCaps={max_out:8,max_seg:24,max_px:1024,gpio_max:48,panel_w:8,panel_h:8,panel_px:64,bri_warn:128,led_data:14};
let patch=[defaultSeg()];
let openSeg=0;
let patchKey='';
let testModes=[];
let testHold=0;
let ledsOwned=false;
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
function outputIndex(rows,i){
  let n=0;
  for(let k=0;k<i;k++) if(!isChild(rows,k)) n++;
  return n;
}
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
  const max=patchCaps.gpio_max;
  const start=patchCaps.led_data;
  if(!used.has(start)) return start;
  for(let i=0;i<=max;i++) if(!used.has(i)) return i;
  return start;
}
function briWarnOn(v){return patchCaps.bri_warn>0&&v>patchCaps.bri_warn;}
function briWarnText(){return 'This '+patchCaps.panel_w+'×'+patchCaps.panel_h+' can overheat above '+patchCaps.bri_warn+'.';}
function segsFromStatus(s){
  if(s.outputs&&s.outputs.length){
    const rows=[];
    s.outputs.forEach(o=>{
      (o.segs||[]).forEach(seg=>{
        const proto=seg.proto||'auto';
        rows.push({
          proto:proto,chip:o.chip||'ws2812b',data:o.data,clk:o.clk||0,
          count:seg.count||patchCaps.panel_px||64,white:!!seg.white,cct:!!seg.cct,order:seg.order||'grb',
          uni:proto==='sacn'?(seg.sacn||1):(seg.artnet||0),ch:seg.ch||1,
          bri:seg.bri!=null?seg.bri:10
        });
      });
    });
    if(rows.length) return rows;
  }
  const m=s.map||{};
  return [{
    proto:m.proto||'auto',chip:m.chip||'ws2812b',data:m.data!=null?m.data:patchCaps.led_data,clk:m.clk||0,
    count:m.count||patchCaps.panel_px||64,white:!!m.white,cct:!!m.cct,order:m.order||'grb',
    uni:m.artnet!=null?m.artnet:0,ch:m.ch||1,bri:m.bri!=null?m.bri:10
  }];
}
function uniHint(proto){
  return proto==='sacn'?'sACN universe (1-based).':'Art-Net 0-based. Resolume “1.1” is often 0.1.';
}
function chPx(row){
  return 3+(row.white?1:0)+(row.cct?1:0);
}
function dmxSpan(row){
  const px=Math.max(1,row.count|0);
  const startCh=Math.max(1,Math.min(512,row.ch|0));
  const startUni=Math.max(0,row.uni|0);
  const last=(startCh-1)+px*chPx(row)-1;
  const endUni=startUni+Math.floor(last/512);
  const endCh=(last%512)+1;
  return startUni+'.'+startCh+'\u2013'+endUni+'.'+endCh;
}
function uniReadout(row){
  return (row.proto==='sacn'?'sACN':'Art-Net')+' '+dmxSpan(row)+' \u00b7 '+chPx(row)+' ch/px';
}
function patchTitle(row,child,totals){
  const px=child?row.count:(totals&&totals[row.data]!=null?totals[row.data]:row.count);
  return 'GPIO '+row.data+' \u00b7 '+(row.chip||'').toUpperCase()+' \u00b7 '+px+' px \u00b7 '+dmxSpan(row);
}
function refreshPatchTitles(){
  if(!patchList) return;
  readPatchDom();
  const totals=groupCounts(patch);
  patchList.querySelectorAll('.patch').forEach(card=>{
    const i=+card.dataset.i;
    const row=patch[i];
    if(!row) return;
    const titleEl=card.querySelector('.patchhead b');
    if(titleEl) titleEl.textContent=patchTitle(row,isChild(patch,i),totals);
    const read=card.querySelector('.readout');
    if(read) read.textContent=uniReadout(row);
  });
}
function setTxt(id,v){const el=document.getElementById(id);if(el) el.textContent=v==null||v===''?'—':String(v);}
let pollTimer=0,briTimer=0,liveTimer=0,mapTimer=0,mapSaveNoteTimer=0;
let briDirty=false,liveDirty=false,playDirty=false,nameDirty=false,mapDirty=false,bandDirty=false,scanning=false;
let lastNets=[],credsFilled=false,liveSsid='',liveLink='',selBssid='',selCh=0;
let patchSavePending=false;
let playSrc='root',playPath='/',bootSrc='root',bootPath='/',playListKey='',lastFiles=[],lastTitles=[],lastMarks=[],lastDirs=[],syncMaster=false,syncFollow=false;
let playSel=new Set(['root\t/']),playAnchor='root\t/',playCollapsed={},playPaused=false,playNow='',titleEdit=null;
let cfgSrc='root',cfgPath='/';
let tab='live',setupScanned=false,lastName='dmxwhip',streamLive=false,playHold=false;
function setStatus(t,cls){statusEl.className=cls||'';statusEl.textContent=t||'';}
function setNote(t,cls){noteEl.className=t?(cls||'err')+' on':'';noteEl.textContent=t||'';}
function patchSaveFlag(on){
  try{ if(on) sessionStorage.setItem('patchSaved','1'); else sessionStorage.removeItem('patchSaved'); }catch(e){}
}
function patchSaveFlagOn(){
  try{ return sessionStorage.getItem('patchSaved')==='1'; }catch(e){ return false; }
}
function markPatchSaving(){
  patchSavePending=true;
  setNote('Saved.','ok');
  clearTimeout(mapSaveNoteTimer);
  mapSaveNoteTimer=setTimeout(()=>{ if(noteEl.textContent==='Saved.') setNote(''); },4000);
}
function finishPatchSave(){
  if(!patchSavePending&&!patchSaveFlagOn()) return;
  patchSavePending=false;
  patchSaveFlag(false);
  if(mapSave){
    mapSave.textContent='Save';
    mapSave.disabled=false;
  }
  setNote('Saved.','ok');
  clearTimeout(mapSaveNoteTimer);
  mapSaveNoteTimer=setTimeout(()=>{ if(noteEl.textContent==='Saved.') setNote(''); },4000);
}
function dropHint(){
  if(!patchSavePending&&!patchSaveFlagOn()) setNote('Page dropped. Rejoin dmxwhip and open http://4.3.2.1','err');
  clearTimeout(pollTimer);
  pollTimer=setTimeout(poll,1000);
}
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
function fileBase(p){return String(p||'').split('/').pop()||'';}
function parentDir(p){
  const s=String(p||'');
  const i=s.lastIndexOf('/');
  if(i<=0) return '/';
  return s.slice(0,i);
}
function rowKey(src,path){return src+'\t'+path;}
function fileTitle(p){
  const i=lastFiles.indexOf(p);
  const t=i>=0&&lastTitles[i]?String(lastTitles[i]).trim():'';
  return t||displayName(p);
}
function showBri(){}
function showPlayOpts(){
  fileopts.className=playSrc==='file'?'on':'';
  folderopts.className=playSrc==='folder'?'on':'';
  foldernrow.className=(playSrc==='folder'&&folderrepEl.value==='count')?'on':'';
}
function playRows(){
  const dirs=lastDirs.slice().sort();
  const files=lastFiles.slice();
  const rows=[{src:'root',path:'/',depth:0,kind:'root'}];
  function kids(parent){
    return {
      dirs:dirs.filter(d=>parentDir(d)===parent),
      files:files.filter(f=>parentDir(f)===parent)
    };
  }
  function walk(parent,depth){
    const k=kids(parent);
    k.dirs.forEach(d=>{
      rows.push({src:'folder',path:d,depth:depth,kind:'dir'});
      if(!playCollapsed[d]) walk(d,depth+1);
    });
    k.files.forEach(f=>rows.push({src:'file',path:f,depth:depth,kind:'dmx'}));
  }
  walk('/',1);
  return rows;
}
function markPlaySel(){
  const kids=plist.children;
  for(let i=0;i<kids.length;i++){
    const el=kids[i];
    if(!el.dataset||!el.dataset.src) continue;
    const key=rowKey(el.dataset.src,el.dataset.path);
    const on=playSel.has(key);
    const now=el.dataset.src==='file'&&playNow&&el.dataset.path===playNow;
    const boot=el.dataset.src===bootSrc&&(bootSrc==='root'||el.dataset.path===bootPath);
    el.className='playrow'+(on?' sel':'')+(now?' now':'')+(boot?' boot':'');
    let badge=el.querySelector('.playboot');
    if(boot){
      if(!badge){
        badge=document.createElement('span');
        badge.className='playboot';
        badge.textContent='Startup';
        el.appendChild(badge);
      }
    }else if(badge) badge.remove();
  }
}
function beginTitleEdit(el,path){
  if(titleEdit) return;
  titleEdit=path;
  const name=el.querySelector('.playname');
  if(!name) return;
  const input=document.createElement('input');
  input.maxLength=48;
  input.value=fileTitle(path);
  input.onclick=e=>e.stopPropagation();
  const done=(save)=>{
    if(titleEdit!==path) return;
    titleEdit=null;
    const raw=save?input.value.trim():'';
    if(save&&raw!==fileTitle(path)){
      postForm('/meta',{path:path,name:raw}).then(async r=>{
        const s=await r.json();
        if(!r.ok){setNote(s.error||'Rename failed','err');return;}
        setNote('');
        applyMeta(s);
      }).catch(dropHint);
      return;
    }
    paintPlayList();
  };
  input.onkeydown=e=>{
    if(e.key==='Enter'){e.preventDefault();input.blur();}
    if(e.key==='Escape'){e.preventDefault();done(false);}
  };
  input.onblur=()=>done(true);
  name.replaceWith(input);
  input.focus();
  input.select();
}
function onPlayRow(ev,src,path){
  if(titleEdit) return;
  ev.preventDefault();
  const key=rowKey(src,path);
  const rows=playRows();
  const keys=rows.map(r=>rowKey(r.src,r.path));
  if(ev.shiftKey&&playAnchor){
    const a=keys.indexOf(playAnchor);
    const b=keys.indexOf(key);
    if(a>=0&&b>=0){
      const lo=Math.min(a,b),hi=Math.max(a,b);
      playSel=new Set(keys.slice(lo,hi+1));
    }else{
      playSel=new Set([key]);
      playAnchor=key;
    }
  }else if(ev.ctrlKey||ev.metaKey){
    if(playSel.has(key)) playSel.delete(key);
    else playSel.add(key);
    if(!playSel.size) playSel.add(key);
  }else{
    if(playSel.size===1&&playSel.has(key)&&playSrc===src&&playPath===path&&src==='file'){
      beginTitleEdit(ev.currentTarget,path);
      return;
    }
    playSel=new Set([key]);
    playAnchor=key;
  }
  playDirty=true;
  playSrc=src;
  playPath=path;
  markPlaySel();
  showPlayOpts();
}
function paintPlayList(){
  plist.innerHTML='';
  const rows=playRows();
  rows.forEach(r=>{
    const d=document.createElement('div');
    d.dataset.src=r.src;
    d.dataset.path=r.path;
    d.style.paddingLeft=(8+r.depth*14)+'px';
    if(r.src==='folder'){
      const open=!playCollapsed[r.path];
      d.innerHTML='<button type="button" class="chev" aria-label="Toggle">'+(open?'▾':'▸')+'</button><span class="playname">'+escapeHtml(fileBase(r.path))+'</span>';
      d.querySelector('.chev').onclick=ev=>{
        ev.stopPropagation();
        playCollapsed[r.path]=!playCollapsed[r.path];
        paintPlayList();
      };
    }else if(r.src==='file'){
      const mi=lastFiles.indexOf(r.path);
      const mk=(mi>=0&&lastMarks[mi])||'';
      let badges='';
      if(mk==='split') badges+='<span class="playmark">Split</span>';
      else if(mk==='uni') badges+='<span class="playmark">Copy</span>';
      if(playNow&&r.path===playNow){
        if(syncFollow) badges+='<span class="playrole">Slave</span>';
        else if(syncMaster) badges+='<span class="playrole">Master</span>';
      }
      d.innerHTML='<span class="playname">'+escapeHtml(fileTitle(r.path))+'</span><span class="playfile">'+escapeHtml(fileBase(r.path))+'</span>'+badges;
    }else{
      d.innerHTML='<span class="playname">All looks</span>';
    }
    d.onclick=ev=>onPlayRow(ev,r.src,r.path);
    plist.appendChild(d);
  });
  if(!lastFiles.length&&!lastDirs.length){
    const e=document.createElement('p');
    e.className='hint';
    e.textContent='No .dmx files.';
    plist.appendChild(e);
  }
  markPlaySel();
}
function renderPlayList(p){
  lastFiles=p.files||[];
  lastDirs=p.dirs||[];
  lastTitles=p.titles||[];
  lastMarks=p.marks||[];
  playListKey=lastFiles.join('\n')+'|'+lastDirs.join('\n')+'|'+lastTitles.join('\n');
  if(titleEdit) return;
  paintPlayList();
}
function startupLabel(){
  if(bootSrc==='none') return 'none';
  if(bootSrc==='file') return fileTitle(bootPath);
  if(bootSrc==='folder') return fileBase(bootPath)||bootPath;
  return 'All looks';
}
function applyPlay(s){
  const p=s.play;
  if(!p){playnowEl.textContent='Now stopped';playNow='';playPaused=false;if(bootnowEl) bootnowEl.textContent='Startup All looks';return;}
  playNow=p.now||'';
  playPaused=!!p.paused;
  cfgSrc=p.src||'root';
  cfgPath=p.path||'/';
  const sy=p.sync||{};
  syncMaster=!!sy.master;
  syncFollow=!!sy.follow;
  const b=p.boot||{};
  bootSrc=b.src||p.src||'root';
  bootPath=b.path||(bootSrc==='root'?'/':(p.path||'/'));
  if(!playDirty){
    playSrc=p.src||'root';
    playPath=p.path||'/';
    playSel=new Set([rowKey(playSrc,playPath)]);
    playAnchor=rowKey(playSrc,playPath);
    if(p.file_loop) fileloopEl.value=p.file_loop;
    if(p.folder_rep) folderrepEl.value=p.folder_rep;
    if(typeof p.n==='number') foldernEl.value=String(p.n);
  }
  showPlayOpts();
  renderPlayList(p);
  if(bootnowEl) bootnowEl.textContent='Startup '+startupLabel();
  if(playPaused&&p.now) playnowEl.textContent='Paused '+fileTitle(p.now);
  else playnowEl.textContent=p.now?'Now '+fileTitle(p.now):'Now stopped';
}
let snDirty=false;
function snShow(){
  const role=document.getElementById('snRole').value;
  document.getElementById('snFields').hidden=role==='standalone';
  document.getElementById('snCh').parentNode&&(document.getElementById('snCh').disabled=role!=='host');
}
function applyShowNet(s){
  const sn=s.shownet;
  if(!sn||snDirty) return;
  document.getElementById('snRole').value=sn.role||'standalone';
  document.getElementById('snSsid').value=sn.ssid||'';
  if(sn.ch) document.getElementById('snCh').value=String(sn.ch);
  snShow();
}
function saveShowNet(){
  const role=document.getElementById('snRole').value;
  const btn=document.getElementById('snSave');
  if(!window.confirm('Save the show network and reboot this node?')) return;
  btn.disabled=true;
  postForm('/shownet',{role,ssid:document.getElementById('snSsid').value.trim(),pass:document.getElementById('snPass').value,ch:document.getElementById('snCh').value}).then(async r=>{
    const s=await r.json().catch(()=>({}));
    if(!r.ok){btn.disabled=false;setNote(s.error||'Save failed','err');return;}
    snDirty=false;
    setNote(role==='host'?'Rebooting as Show Host. Join the show network, then open http://10.77.0.1':'Rebooting…');
  }).catch(()=>{btn.disabled=false;dropHint();});
}
let otaBusy=false;
function fwSize(n){return n>=1048576?(n/1048576).toFixed(2)+' MB':Math.round(n/1024)+' KB';}
function applyOta(s){
  const o=s.ota;
  if(!o) return;
  otaBusy=!!o.busy;
  setTxt('fwInfo','v'+s.ver+' · '+s.board+(o.max?' · slot '+fwSize(o.max):' · no update slot')+(o.pending?' · new, checking itself':''));
  document.getElementById('fwRb').classList.toggle('on',!!o.rolled_back);
  const p=s.ota_peers;
  const running=!!(p&&p.state==='running');
  document.getElementById('fwPeers').disabled=running||!!o.pending;
  const rows=document.getElementById('fwRows');
  if(!p||p.state==='idle'){rows.innerHTML='';return;}
  let h=running
    ?'<p class="readout">Updating other nodes'+(p.total?' · sending '+Math.round(100*p.sent/p.total)+'%':'')+'</p>'
    :'<p class="readout">'+escapeHtml(p.msg||p.state)+'</p>';
  (p.rows||[]).forEach(r=>{
    h+='<p class="readout">'+escapeHtml(r.name||r.ip)+' · v'+escapeHtml(r.from||'?')+' · '+escapeHtml(r.st)+'</p>';
  });
  rows.innerHTML=h;
}
function fwErr(e){
  return ({
    busy:'This node is busy (playing, live or updating others). Stop it first, or confirm to update anyway.',
    'too large':'The file is bigger than this node’s update slot. Flash it once over USB with the companion Flash tab.',
    'other board':'That firmware is for a different board.',
    'not a whip image':'That file is not DMX whIP firmware.',
    'not firmware':'That file is not a firmware image.',
    'no ota slot':'This node’s flash layout has no update slot. Flash it once over USB.',
    'verify failed':'The image did not verify. Nothing changed; try the upload again.'
  })[e]||('Update failed: '+(e||'no reply'));
}
document.getElementById('fwUp').onclick=()=>{
  const fileEl=document.getElementById('fwFile');
  const f=fileEl.files&&fileEl.files[0];
  if(!f){setNote('Choose a firmware .bin first.','err');return;}
  const force=otaBusy;
  if(!window.confirm(force
    ?'This node is playing or receiving live DMX. Update it anyway? The lights stop while it updates and reboots.'
    :'Update this node with '+f.name+'? It reboots itself when done.')) return;
  const btn=document.getElementById('fwUp');
  const wrap=document.getElementById('fwBarWrap');
  const bar=document.getElementById('fwBar');
  const fd=new FormData();
  fd.append('firmware',f,f.name);
  const xhr=new XMLHttpRequest();
  btn.disabled=true;
  wrap.hidden=false;
  bar.style.width='0%';
  xhr.upload.onprogress=e=>{if(e.lengthComputable) bar.style.width=Math.round(100*e.loaded/e.total)+'%';};
  xhr.onload=()=>{
    let s={};
    try{s=JSON.parse(xhr.responseText);}catch(e){}
    if(xhr.status===200){
      bar.style.width='100%';
      setNote('Updated to v'+(s.ver||'?')+'. Rebooting; this page reconnects by itself.','ok');
      return;
    }
    btn.disabled=false;
    wrap.hidden=true;
    setNote(fwErr(s.error),'err');
  };
  xhr.onerror=()=>{btn.disabled=false;wrap.hidden=true;dropHint();};
  xhr.open('POST','/ota?force='+(force?1:0));
  xhr.send(fd);
};
document.getElementById('fwPeers').onclick=()=>{
  const force=document.getElementById('fwForce').checked;
  if(!window.confirm('Send this firmware to every older node of this board'+(force?', including busy ones':'')+'? They reboot themselves.')) return;
  postForm('/ota/peers?force='+(force?1:0),{}).then(async r=>{
    const s=await r.json().catch(()=>({}));
    if(!r.ok){
      setNote(({'no peers':'No other nodes heard on the network.',unverified:'This node is still checking its own new firmware. Try again in a minute.',distributing:'Wait for Distribute to finish.',streaming:'Stop the stream first.'})[s.error]||(s.error||'Update failed'),'err');
      return;
    }
    setNote('Updating other nodes…','ok');
    applyOta(s);
  }).catch(dropHint);
};
function applyLive(s){
  applyShowNet(s);
  applyDist(s);
  applyOta(s);
  if(liveDirty) return;
  if(typeof s.fps==='number') fpsEl.value=String(s.fps);
  if(typeof s.buf==='number') bufEl.value=String(s.buf);
  if(s.park) parkEl.value=s.park;
  if(takeoverEl&&s.takeover) takeoverEl.value=s.takeover;
  if(unisyncEl&&s.unisync) unisyncEl.value=s.unisync;
  if(lossEl&&s.loss) lossEl.value=s.loss;
}
function linkLabel(v){
  if(v==='5g') return '5 GHz';
  if(v==='wired') return 'Wired';
  if(v==='2g') return '2.4 GHz';
  return '—';
}
function applyBand(s){
  if(bandRow) bandRow.hidden=!s.wifi_5g;
  if(bandEl && s.wifi_5g && s.band && !bandDirty) bandEl.value=s.band;
  if(setupHint){
    setupHint.innerHTML=s.wifi_5g
      ? 'This board can use 2.4 or 5 GHz. Auto/5 GHz may drop USB-JTAG. If this sheet closes, rejoin <b>dmxwhip</b> or open http://4.3.2.1'
      : '2.4 GHz only. If this sheet closes, rejoin <b>dmxwhip</b> or open http://4.3.2.1';
  }
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
function fillCreds(s){
  if(credsFilled) return;
  if(!s.saved) return;
  ssidEl.value=s.saved;
  if(typeof s.pass==='string') passEl.value=s.pass;
  credsFilled=true;
}
function applyChrome(s){
  if(s.ver) verEl.textContent='dmxwhip v'+s.ver;
  fillCreds(s);
  showName(s.name);
  streamLive=!!s.live;
  playHold=!!(s.play&&s.play.hold);
  const mode=s.mode||(playHold?'play':(streamLive?'live':(s.play&&s.play.now?'play':'idle')));
  modeEl.className='mode '+mode;
  if(modeLab) modeLab.textContent=mode;
  const streamOwns=streamLive&&!playHold;
  ledsOwned=streamOwns;
  document.querySelectorAll('.testrow button').forEach(b=>{b.disabled=ledsOwned;});
  const toStream=document.getElementById('toStream');
  if(toStream) toStream.className=playHold?'on':'';
  idBtn.disabled=streamOwns;
  idBtn.title=streamOwns?'Unavailable while live':'';
  const delBtn=document.getElementById('del');
  if(delBtn){
    delBtn.disabled=streamOwns;
    delBtn.title=streamOwns?'Unavailable while live':'';
  }
  if(s.saved){savedRow.className='on';savedLab.textContent='saved '+s.saved+' (connects at boot)'+(s.ip?(' · STA '+s.ip):'');}
  else {savedRow.className='';savedLab.textContent='';}
  applyLive(s);
  applyBand(s);
}
// ---------------------------------------------------------------- advanced patch
const FXH={dim:['Master dimmer','0-255 · open until first raised'],int:['Intensity','0-255 · open until first raised'],strobe:['Strobe','0-9 open · 10-255 = 1-25 Hz'],hue:['Hue shift','0 none · 1-255 round the colour wheel'],folder:['Folder','0 SD root · n = n-th folder (A-Z)'],clip:['Clip','0 normal playback · n = n-th look in the folder (A-Z)']};
const FX_HDR=[FXH.dim,FXH.strobe,['Strobe colour','0 white · 1-255 round the colour wheel'],['Strobe intensity','Off-phase level of the strobe colour · 0 blackout'],FXH.hue,['Filter red','Red removed · 0 none'],['Filter green','Green removed · 0 none'],['Filter blue','Blue removed · 0 none'],['Add red','Red added · 0 none'],['Add green','Green added · 0 none'],['Add blue','Blue added · 0 none'],FXH.folder,FXH.clip];
const FX_HDR_BASIC=[FXH.int,FXH.strobe,FXH.hue,FXH.folder,FXH.clip];
function fxHdr(){return fx.mode==='basic'?FX_HDR_BASIC:FX_HDR;}
const fxEls={};
['pxTabMain','pxTabAdv','pxMainBox','fxBox','fxEn','fxMode','fxModeHint','fxProto','fxUni','fxCh','fxFoot','fxErr','fxHdr','fxSubs','fxTree','fxSel','fxGroup','fxAddTo','fxAdd','fxUngroup','fxLocate','fxClear','fxSave'].forEach(id=>{fxEls[id]=document.getElementById(id);});
let fxView=false;
let fx={en:false,mode:'dim',proto:'artnet',uni:0,ch:1,subs:[]};
let fxSubOf=[];
let fxNames=[];
let fxNamesDirty=false;
let fxDirty=false;
let fxLoaded=false;
let fxLoading=false;
let fxOuts=[];
let fxOutsKey='';
let fxPx=[];
let fxSel=new Set();
let fxAnchor=-1;
const fxOpen=new Set();
function fxBuildPixels(outs){
  const px=[];
  (outs||[]).forEach((o,oi)=>{
    (o.segs||[]).forEach((seg,si)=>{
      const cpp=seg.ch_px||(3+(seg.white?1:0)+(seg.cct?1:0));
      for(let i=0;i<(seg.count||0);i++) px.push({out:oi,seg:si,cpp});
    });
  });
  return px;
}
function fxNoteOutputs(s){
  if(!Array.isArray(s.outputs)) return;
  const key=JSON.stringify(s.outputs.map(o=>[o.data,(o.segs||[]).map(g=>[g.count,g.ch_px])]));
  if(key===fxOutsKey) return;
  fxOutsKey=key;
  fxOuts=s.outputs;
  fxPx=fxBuildPixels(fxOuts);
  const next=new Array(fxPx.length).fill(-1);
  for(let g=0;g<next.length&&g<fxSubOf.length;g++) next[g]=fxSubOf[g];
  fxSubOf=next;
  fxSel=new Set([...fxSel].filter(g=>g<fxPx.length));
  if(fxView) fxRender();
}
function fxParseRanges(str,cb){
  String(str||'').split(',').forEach(part=>{
    const m=/^\s*(\d+)\s*(?:-\s*(\d+))?\s*$/.exec(part);
    if(!m) return;
    const a=+m[1],b=m[2]!=null?+m[2]:a;
    for(let g=a;g<=b;g++) cb(g);
  });
}
function fxRanges(list){
  const out=[];
  let a=-1,b=-1;
  list.slice().sort((x,y)=>x-y).forEach(g=>{
    if(a>=0&&g===b+1){b=g;return;}
    if(a>=0) out.push(a===b?String(a):a+'-'+b);
    a=b=g;
  });
  if(a>=0) out.push(a===b?String(a):a+'-'+b);
  return out.join(',');
}
function fxPixelsOf(k){
  const out=[];
  fxSubOf.forEach((v,g)=>{if(v===k) out.push(g);});
  return out;
}
function fxPer(){return fx.mode==='dim'?2:(fx.mode==='rgb'?5:0);}
// Same rules as fixture.cpp: header first; Full never splits a pixel across a universe.
function fxLayout(){
  const base=Math.max(0,(fx.ch|0)-1);
  const lay={addr:null,footprint:0,unis:1,err:''};
  if(fx.mode==='full'){
    let pos=base+fxHdr().length;
    if(pos>512) lay.err='The header does not fit after this channel.';
    lay.addr=new Array(fxPx.length);
    fxPx.forEach((p,g)=>{
      if((pos%512)+p.cpp>512) pos=(Math.floor(pos/512)+1)*512;
      lay.addr[g]=pos;
      pos+=p.cpp;
    });
    lay.unis=Math.max(1,Math.ceil(pos/512));
    lay.footprint=pos-base;
    if(!lay.err&&lay.unis>6) lay.err='Full mode needs more than 6 universes. Use a reduced mode or fewer pixels.';
  }else{
    lay.footprint=fxHdr().length+fxPer()*fx.subs.length;
    if(base+lay.footprint>512) lay.err='The fixture does not fit in the universe from this channel.';
  }
  return lay;
}
function fxChText(rel,n){
  const u=Math.floor(rel/512);
  const first=rel%512+1;
  const list=[];
  for(let i=0;i<n;i++) list.push(first+i);
  return (u>0?'U'+((fx.uni|0)+u)+': ':'')+list.join(',');
}
function fxSubCh(k){
  const per=fxPer();
  if(!per) return '—';
  return fxChText((fx.ch|0)-1+fxHdr().length+per*k,per);
}
function fxPixelCh(g,lay){
  if(fx.mode==='full') return lay.addr&&lay.addr[g]!=null?fxChText(lay.addr[g],fxPx[g].cpp):'—';
  const k=fxSubOf[g];
  return k>=0?fxSubCh(k):'none (not grouped)';
}
function fxFootText(lay){
  const base=(fx.ch|0);
  const name={basic:'Basic',dim:'Dim + FX',rgb:'RGB + FX',full:'Full'}[fx.mode]||'Dim + FX';
  const hl=fxHdr().length;
  const math=fx.mode==='full'
    ?hl+' + '+fxPx.length+' px'
    :(fx.mode==='basic'?String(hl):hl+' + '+fxPer()+'×'+fx.subs.length);
  const last=base-1+lay.footprint-1;
  const endU=(fx.uni|0)+Math.floor(last/512);
  return name+' · '+math+' = '+lay.footprint+' ch · '+(fx.proto==='sacn'?'sACN':'Art-Net')+' '+(fx.uni|0)+'.'+base+'–'+endU+'.'+(last%512+1)+(lay.unis>1?' · '+lay.unis+' universes':'');
}
function fxFillForm(){
  fxEls.fxEn.checked=!!fx.en;
  fxEls.fxMode.value=fx.mode;
  fxEls.fxProto.value=fx.proto;
  fxEls.fxUni.value=String(fx.uni);
  fxEls.fxCh.value=String(fx.ch);
}
function fxRenderSubs(){
  const opts=fx.subs.map((s,k)=>'<option value="'+k+'">'+escapeHtml(s.name||('Sub '+(k+1)))+'</option>').join('');
  fxEls.fxAddTo.innerHTML=opts||'<option value="">No sub-fixtures</option>';
  if(!fx.subs.length){
    fxEls.fxSubs.innerHTML='<p class="hint">None yet. Select pixels below, then Group as new.</p>';
    return;
  }
  let h='';
  fx.subs.forEach((s,k)=>{
    const n=fxPixelsOf(k).length;
    h+='<div class="fxsub" data-k="'+k+'">'+
      '<input data-f="subname" value="'+escapeHtml(s.name)+'" maxlength="23" aria-label="Sub-fixture '+(k+1)+' name">'+
      '<div class="patchops">'+
        '<button data-a="sel" type="button">Select</button>'+
        '<button data-a="up" type="button"'+(k===0?' disabled':'')+' aria-label="Move up">↑</button>'+
        '<button data-a="down" type="button"'+(k===fx.subs.length-1?' disabled':'')+' aria-label="Move down">↓</button>'+
        '<button data-a="del" type="button" aria-label="Delete">×</button>'+
      '</div>'+
      '<span class="fxmeta">'+(k+1)+' · '+n+' px · ch '+fxSubCh(k)+'</span>'+
    '</div>';
  });
  fxEls.fxSubs.innerHTML=h;
}
function fxPixelRow(g,lay){
  const k=fxSubOf[g];
  const sub=k>=0?fx.subs[k]:null;
  return '<div class="fxpx'+(fxSel.has(g)?' sel':'')+'" data-g="'+g+'">'+
    '<span class="fxidx">'+g+'</span>'+
    '<input data-f="pxname" value="'+escapeHtml(fxNames[g]||'')+'" placeholder="Pixel '+g+'" maxlength="23" aria-label="Pixel '+g+' name">'+
    '<span class="fxmeta">'+(sub?escapeHtml(sub.name)+' · ':'')+'ch '+fxPixelCh(g,lay)+'</span>'+
  '</div>';
}
function fxRenderTree(lay){
  if(!fxPx.length){
    fxEls.fxTree.innerHTML='<p class="hint">No pixels. Save a main patch first.</p>';
    return;
  }
  let h='';
  let g0=0;
  fxOuts.forEach((o,oi)=>{
    (o.segs||[]).forEach((seg,si)=>{
      const n=seg.count||0;
      const key=oi+'.'+si;
      const open=fxOpen.has(key);
      h+='<div class="fxseg" data-key="'+key+'" data-g0="'+g0+'" data-n="'+n+'">'+
        '<div class="fxseghead"><b>Output '+(oi+1)+' · GPIO '+o.data+(o.segs.length>1?' · part '+(si+1):'')+' · px '+g0+'–'+(g0+n-1)+'</b>'+
        '<div class="patchops"><button data-a="selseg" type="button">Select</button><button data-a="toggle" type="button">'+(open?'Hide':'Show')+'</button></div></div>';
      if(open){
        for(let g=g0;g<g0+n;g++) h+=fxPixelRow(g,lay);
      }
      h+='</div>';
      g0+=n;
    });
  });
  fxEls.fxTree.innerHTML=h;
}
function fxSelText(){
  const list=[...fxSel];
  const has=list.length>0;
  fxEls.fxSel.textContent=has
    ?list.length+' selected: '+fxRanges(list)
    :'Tap pixels to select; Shift-tap selects a run.';
  fxEls.fxGroup.disabled=!has;
  fxEls.fxAdd.disabled=!has||!fx.subs.length;
  fxEls.fxUngroup.disabled=!has;
  fxEls.fxLocate.disabled=!has;
  fxEls.fxClear.disabled=!has;
}
const FX_MODE_HINT={
  basic:'Five channels over what the node already plays: intensity, strobe, hue shift, folder and clip. No sub-fixtures. It uses its own universe, which never takes over playback; with no console the show plays untouched.',
  dim:'Overlays what the node already plays: its SD show, a synced group, or the live stream on its main patch. The console adds the header effects and a dimmer + strobe per sub-fixture (2 ch each); with no sub-fixtures it is a pure overlay. It uses its own universe, which never takes over playback; with no console the show plays untouched.',
  rgb:'The console colours each sub-fixture (dim, strobe, red, green, blue, 5 ch each). The node’s own show is not used, and pixels in no sub-fixture stay dark.',
  full:'The console drives every LED directly after the 13 header channels: red, green, blue (then white, warm white) per pixel, whatever order the strip wires them in. It continues into the next universes without splitting a pixel (up to 6).'
};
// Channel span as text: 21-25, U3: 1-40, U2: 500 - U3: 20 (same as the
// companion's fixture.js spanText).
function fxSpan(a,b){
  const at=r=>{const u=Math.floor(r/512);return{u,ch:r%512+1,t:u>0?'U'+((fx.uni|0)+u)+': ':''};};
  const x=at(a),y=at(b);
  if(a===b) return x.t+x.ch;
  if(x.u===y.u) return x.t+x.ch+'-'+y.ch;
  return (x.t||'U'+(fx.uni|0)+': ')+x.ch+' - '+y.t+y.ch;
}
// Every channel's function: header, then one row per sub-fixture or segment.
function fxMapRows(lay){
  const base=Math.max(0,(fx.ch|0)-1);
  const rows=fxHdr().map((h,i)=>[fxSpan(base+i,base+i),h[0],h[1]]);
  if(fx.mode==='full'){
    if(!lay.addr) return rows;
    let g=0;
    (fxOuts||[]).forEach((o,oi)=>(o.segs||[]).forEach((seg,si)=>{
      const n=seg.count||0,cpp=seg.ch_px||(3+(seg.white?1:0)+(seg.cct?1:0));
      if(n){
        // DMX is R, G, B[, W][, warm W]; the chip order is only the wire order.
        let ord='rgb'+((seg.white!=null?seg.white:cpp>=4)?'w':'')+((seg.cct!=null?seg.cct:cpp>=5)?'c':'');
        if(ord.length!==cpp) ord='rgbwc'.slice(0,cpp);
        rows.push([fxSpan(lay.addr[g],lay.addr[g+n-1]+cpp-1),'Out '+(oi+1)+' Seg '+(si+1),
          'px '+g+'-'+(g+n-1)+' · '+cpp+' ch each ('+ord.split('').map(c=>c==='c'?'WW':c.toUpperCase()).join(', ')+')']);
      }
      g+=n;
    }));
    return rows;
  }
  const per=fxPer(),f=fx.mode==='rgb'?'Dim, Strobe, Red, Green, Blue':'Dim, Strobe';
  if(!per) return rows;
  fx.subs.forEach((s,k)=>{
    const r=base+fxHdr().length+per*k;
    rows.push([fxSpan(r,r+per-1),s.name||('Sub '+(k+1)),f+' · '+fxPixelsOf(k).length+' px']);
  });
  return rows;
}
function fxRender(){
  const lay=fxLayout();
  fxEls.fxModeHint.textContent=FX_MODE_HINT[fx.mode]||'';
  document.getElementById('fxBasicNote').hidden=fx.mode!=='basic';
  fxEls.fxFoot.textContent=fxFootText(lay);
  fxEls.fxErr.textContent=lay.err||fxServerErr||'';
  const base=(fx.ch|0);
  fxEls.fxHdr.innerHTML='<tr><th>Channel</th><th>Function</th><th>Values</th></tr>'+
    fxMapRows(lay).map(r=>'<tr><td>'+r[0]+'</td><td>'+escapeHtml(r[1])+'</td><td>'+r[2]+'</td></tr>').join('');
  fxRenderSubs();
  fxRenderTree(lay);
  fxSelText();
}
function fxPaintSel(){
  fxEls.fxTree.querySelectorAll('.fxpx').forEach(el=>{el.classList.toggle('sel',fxSel.has(+el.dataset.g));});
  fxSelText();
}
let fxServerErr='';
function fxApply(f){
  fx={
    en:!!f.en,mode:f.mode||'dim',proto:f.proto||'artnet',
    uni:f.uni|0,ch:f.ch||1,
    subs:(f.subs||[]).map(s=>({name:s.name||''}))
  };
  fxSubOf=new Array(fxPx.length).fill(-1);
  (f.subs||[]).forEach((s,k)=>fxParseRanges(s.px,g=>{if(g<fxSubOf.length) fxSubOf[g]=k;}));
  fxServerErr=f.storage===false
    ?'This node has no config partition. Flash it once over USB.'
    :(f.en&&f.valid===false?(f.err||''):'');
  fxDirty=false;
  fxFillForm();
}
async function fxLoad(){
  if(fxLoading) return;
  fxLoading=true;
  try{
    const f=await jget('/fixture');
    fxApply(f);
    const names=[];
    for(let from=0;from<(f.pixels||0);from+=128){
      const r=await jget('/fixture/names?from='+from+'&n=128');
      (r.names||[]).forEach((n,i)=>{names[from+i]=n;});
    }
    fxNames=names;
    fxNamesDirty=false;
    fxLoaded=true;
    fxRender();
  }catch(e){
    setNote('Could not read the advanced patch.','err');
  }finally{
    fxLoading=false;
  }
}
function fxShow(adv){
  fxView=adv;
  fxEls.pxTabMain.className=adv?'':'on';
  fxEls.pxTabAdv.className=adv?'on':'';
  fxEls.pxMainBox.classList.toggle('off',adv);
  fxEls.fxBox.classList.toggle('off',!adv);
  if(adv){
    if(!fxLoaded&&!fxDirty) fxLoad();
    else fxRender();
  }
}
fxEls.pxTabMain.onclick=()=>fxShow(false);
fxEls.pxTabAdv.onclick=()=>fxShow(true);
function fxFieldsChanged(){
  fx.en=fxEls.fxEn.checked;
  fx.mode=fxEls.fxMode.value;
  fx.proto=fxEls.fxProto.value;
  fx.uni=Math.max(0,parseInt(fxEls.fxUni.value,10)||0);
  fx.ch=Math.min(512,Math.max(1,parseInt(fxEls.fxCh.value,10)||1));
  fxDirty=true;
  fxServerErr='';
  fxRender();
}
['fxEn','fxMode','fxProto'].forEach(id=>{fxEls[id].onchange=fxFieldsChanged;});
['fxUni','fxCh'].forEach(id=>{fxEls[id].onchange=fxFieldsChanged;});
fxEls.fxSubs.addEventListener('input',e=>{
  const row=e.target.closest('.fxsub');
  if(!row||e.target.dataset.f!=='subname') return;
  fx.subs[+row.dataset.k].name=e.target.value;
  fxDirty=true;
});
fxEls.fxSubs.addEventListener('change',e=>{
  if(e.target.dataset.f==='subname') fxRender();
});
fxEls.fxSubs.addEventListener('click',e=>{
  const btn=e.target.closest('button');
  const row=e.target.closest('.fxsub');
  if(!btn||!row) return;
  const k=+row.dataset.k;
  const a=btn.dataset.a;
  if(a==='sel'){
    fxSel=new Set(fxPixelsOf(k));
    fxPaintSel();
    return;
  }
  if(a==='up'||a==='down'){
    const j=a==='up'?k-1:k+1;
    if(j<0||j>=fx.subs.length) return;
    const t=fx.subs[k];fx.subs[k]=fx.subs[j];fx.subs[j]=t;
    fxSubOf=fxSubOf.map(v=>v===k?j:(v===j?k:v));
  }else if(a==='del'){
    fx.subs.splice(k,1);
    fxSubOf=fxSubOf.map(v=>v===k?-1:(v>k?v-1:v));
  }
  fxDirty=true;
  fxRender();
});
fxEls.fxTree.addEventListener('input',e=>{
  if(e.target.dataset.f!=='pxname') return;
  const row=e.target.closest('.fxpx');
  fxNames[+row.dataset.g]=e.target.value;
  fxNamesDirty=true;
});
fxEls.fxTree.addEventListener('click',e=>{
  if(e.target.tagName==='INPUT') return;
  const btn=e.target.closest('button');
  if(btn){
    const seg=btn.closest('.fxseg');
    if(!seg) return;
    if(btn.dataset.a==='toggle'){
      const key=seg.dataset.key;
      if(fxOpen.has(key)) fxOpen.delete(key);
      else fxOpen.add(key);
      fxRenderTree(fxLayout());
      return;
    }
    if(btn.dataset.a==='selseg'){
      const g0=+seg.dataset.g0,n=+seg.dataset.n;
      let all=true;
      for(let g=g0;g<g0+n;g++) all=all&&fxSel.has(g);
      for(let g=g0;g<g0+n;g++){if(all) fxSel.delete(g);else fxSel.add(g);}
      fxPaintSel();
    }
    return;
  }
  const row=e.target.closest('.fxpx');
  if(!row) return;
  const g=+row.dataset.g;
  if(e.shiftKey&&fxAnchor>=0){
    const a=Math.min(fxAnchor,g),b=Math.max(fxAnchor,g);
    for(let i=a;i<=b;i++) fxSel.add(i);
  }else if(fxSel.has(g)){
    fxSel.delete(g);
  }else{
    fxSel.add(g);
  }
  fxAnchor=g;
  fxPaintSel();
});
function fxAssign(k){
  fxSel.forEach(g=>{fxSubOf[g]=k;});
  fxDirty=true;
  fxRender();
}
fxEls.fxGroup.onclick=()=>{
  if(!fxSel.size) return;
  if(fx.subs.length>=96){setNote('96 sub-fixtures is the maximum.','err');return;}
  fx.subs.push({name:'Sub '+(fx.subs.length+1)});
  fxAssign(fx.subs.length-1);
};
fxEls.fxAdd.onclick=()=>{
  const k=parseInt(fxEls.fxAddTo.value,10);
  if(!fxSel.size||!(k>=0)) return;
  fxAssign(k);
};
fxEls.fxUngroup.onclick=()=>fxAssign(-1);
fxEls.fxClear.onclick=()=>{fxSel.clear();fxAnchor=-1;fxPaintSel();};
fxEls.fxLocate.onclick=()=>{
  if(!fxSel.size) return;
  postForm('/fixture/locate',{px:fxRanges([...fxSel]),ms:'10000'}).then(async r=>{
    if(!r.ok){const s=await r.json().catch(()=>({}));setNote(s.error==='live'?'Locate is unavailable while live input is on.':(s.error||'Locate failed'),'err');return;}
    setNote('Selected pixels lit white for 10 s.','ok');
  }).catch(dropHint);
};
fxEls.fxSave.onclick=async()=>{
  const lay=fxLayout();
  if(fx.en&&lay.err){setNote(lay.err,'err');return;}
  const fields={en:fx.en?'1':'0',mode:fx.mode,proto:fx.proto,uni:String(fx.uni),ch:String(fx.ch),n:String(fx.subs.length)};
  fx.subs.forEach((s,k)=>{
    fields['s'+k+'n']=s.name;
    fields['s'+k+'px']=fxRanges(fxPixelsOf(k));
  });
  const btn=fxEls.fxSave;
  btn.disabled=true;
  try{
    const r=await postForm('/fixture',fields);
    const s=await r.json().catch(()=>({}));
    if(!r.ok){setNote('Advanced patch not saved: '+(s.error||'error'),'err');return;}
    if(fxNamesDirty){
      for(let from=0;from<fxPx.length;from+=128){
        const chunk=[];
        for(let g=from;g<Math.min(fxPx.length,from+128);g++) chunk.push(String(fxNames[g]||'').replace(/[\r\n]/g,' '));
        const rn=await postForm('/fixture/names',{from:String(from),names:chunk.join('\n')});
        if(!rn.ok) throw new Error('names');
      }
      fxNamesDirty=false;
    }
    fxApply(s);
    fxRender();
    setNote('Advanced patch saved.','ok');
  }catch(e){
    setNote('Advanced patch saved, but pixel names were not.','err');
  }finally{
    btn.disabled=false;
  }
};
function applyPixels(s){
  if(s.patch){
    patchCaps={
      max_out:s.patch.max_out||8,
      max_seg:s.patch.max_seg||24,
      max_px:s.patch.max_px||1024,
      gpio_max:s.patch.gpio_max||48,
      panel_w:s.patch.panel_w||0,
      panel_h:s.patch.panel_h||0,
      panel_px:s.patch.panel_px||0,
      bri_warn:s.patch.bri_warn||0,
      led_data:s.patch.led_data!=null?s.patch.led_data:14
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
  applyTests(s);
  fxNoteOutputs(s);
}
function applyTests(s){
  if(!Array.isArray(s.outputs)) return;
  if(Date.now()<testHold) return;
  const next=s.outputs.map(o=>(o.test&&o.test!=='off')?o.test:'');
  let same=next.length===testModes.length;
  if(same){
    for(let i=0;i<next.length;i++) if((testModes[i]||'')!==next[i]) same=false;
  }
  if(same) return;
  testModes=next;
  if(!mapDirty) renderPatch();
}
function applyStats(s){
  applyChrome(s);
  applyPixels(s);
  liveSsid=s.ssid||'';
  liveLink=s.link||'';
  setTxt('lrSsid',s.ssid||'no STA');
  setTxt('lrSta',s.ip||'—');
  setTxt('lrRssi',typeof s.rssi==='number'?s.rssi+' dBm':'—');
  setTxt('lrLink',linkLabel(s.link));
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
  setTxt('lpNow',p.now?fileTitle(p.now):'stopped');
  const parkPip=document.getElementById('lpPark');
  if(parkPip) parkPip.className='pip'+(p.parked?' on':'');
  const pausePip=document.getElementById('lpPause');
  if(pausePip) pausePip.className='pip'+(p.paused?' on':'');
  const urPip=document.getElementById('lpUr');
  if(urPip) urPip.className='pip'+(p.underrun?' warn':'');
  setTxt('lpFrame',typeof p.frame==='number'?p.frame:'—');
  setTxt('lhUp',typeof s.up_ms==='number'?fmtUp(s.up_ms):'—');
  setTxt('lhHeap',fmtKb(s.heap));
  setTxt('lhPsram',fmtKb(s.psram));
  if(tab==='setup'&&lastNets.length) paintNetClasses();
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
  const b=new URLSearchParams();
  Object.keys(fields||{}).forEach(k=>{
    const v=fields[k];
    if(Array.isArray(v)) v.forEach(x=>b.append(k,String(x)));
    else if(v!=null) b.append(k,String(v));
  });
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});
}
function bandFilter(){
  return (bandEl&&bandRow&&!bandRow.hidden)?bandEl.value:'2g';
}
function pickTwin(n){
  const mode=bandFilter();
  if(n.both){
    if(mode==='5g'&&n.b5) return n.b5;
    if(mode==='2g'&&n.b2) return n.b2;
    if(n.b5) return n.b5;
    return n.b2||n;
  }
  return n;
}
function mergeNets(nets){
  const mode=bandFilter();
  const raw=nets||[];
  if(mode==='2g') return raw.filter(n=>n.ghz===2);
  if(mode==='5g') return raw.filter(n=>n.ghz===5);
  const map=new Map();
  raw.forEach(n=>{
    let e=map.get(n.ssid);
    if(!e){
      e={ssid:n.ssid,rssi:n.rssi,secure:!!n.secure,ghz:n.ghz,bssid:n.bssid,ch:n.ch,both:false,b2:null,b5:null};
      map.set(n.ssid,e);
    }
    if(n.rssi>e.rssi) e.rssi=n.rssi;
    e.secure=e.secure||!!n.secure;
    const twin={bssid:n.bssid,ch:n.ch,rssi:n.rssi};
    if(n.ghz===5) e.b5=twin;
    else e.b2=twin;
  });
  return Array.from(map.values()).map(e=>{
    if(e.b2&&e.b5){e.both=true;e.ghz=0;}
    return e;
  });
}
function rowGhzLabel(n){
  if(n.both||n.ghz===0) return '2.4 + 5 GHz ';
  if(n.ghz===5) return '5 GHz ';
  if(n.ghz===2) return '2.4 GHz ';
  return '';
}
function rowIsNow(n){
  if(!liveSsid||liveSsid!==n.ssid) return false;
  if(n.both) return true;
  if(liveLink==='5g') return n.ghz===5;
  if(liveLink==='2g') return n.ghz===2;
  return true;
}
function paintNetClasses(){
  const rows=list.children;
  for(let i=0;i<rows.length;i++){
    const name=rows[i].dataset.ssid||'';
    const ghz=+rows[i].dataset.ghz;
    const both=rows[i].dataset.both==='1';
    rows[i].classList.toggle('sel',ssidEl.value===name);
    const now=!!liveSsid&&liveSsid===name&&(both||!liveLink||liveLink==='wired'||(liveLink==='5g'&&ghz===5)||(liveLink==='2g'&&ghz===2));
    rows[i].classList.toggle('now',now);
  }
}
function renderNets(nets){
  if(nets) lastNets=nets;
  const rows=mergeNets(lastNets);
  list.innerHTML='';
  rows.forEach(n=>{
    const d=document.createElement('div');
    d.dataset.ssid=n.ssid;
    d.dataset.ghz=String(n.ghz||0);
    d.dataset.both=n.both?'1':'0';
    d.className='net';
    d.innerHTML='<span>'+escapeHtml(n.ssid)+'</span><span class="readout">'+rowGhzLabel(n)+(n.secure?'lock ':'open ')+n.rssi+' dBm</span>';
    d.onclick=()=>{
      ssidEl.value=n.ssid;
      const t=pickTwin(n);
      selBssid=t&&t.bssid?t.bssid:'';
      selCh=t&&t.ch?t.ch:0;
      paintNetClasses();
    };
    list.appendChild(d);
  });
  paintNetClasses();
  if(!rows.length){
    if((lastNets||[]).length&&bandFilter()==='5g') setStatus('No 5 GHz networks in this scan. Scan again.');
    else if((lastNets||[]).length&&bandFilter()==='2g') setStatus('No 2.4 GHz networks in this scan. Scan again.');
    else setStatus('No networks found. Try Scan again.');
  }
}
function escapeHtml(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
async function scan(force){
  scanning=true;
  setStatus('Scanning…');
  try{
    const q=force?'?start=1&band='+encodeURIComponent(bandFilter()):'';
    await jget('/scan'+q);
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
    if(patchSavePending||patchSaveFlagOn()) finishPatchSave();
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
    const fields={ssid,password};
    if(bandEl&&bandRow&&!bandRow.hidden) fields.band=bandEl.value;
    if(selBssid){fields.bssid=selBssid;if(selCh) fields.ch=String(selCh);}
    const r=await postForm('/connect',fields);
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
    ssidEl.value='';
    passEl.value='';
    credsFilled=false;
    selBssid='';
    selCh=0;
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
function postBand(){
  if(!bandEl||(bandRow&&bandRow.hidden)) return Promise.resolve();
  return postForm('/band',{band:bandEl.value})
    .then(async r=>{if(!r.ok) throw new Error('http');bandDirty=false;applyMeta(await r.json());})
    .catch(()=>{bandDirty=false;dropHint();});
}
function postLive(){
  return postForm('/live',{fps:fpsEl.value,buf:bufEl.value,park:parkEl.value,takeover:takeoverEl?takeoverEl.value:'yes',unisync:unisyncEl?unisyncEl.value:'no',loss:lossEl?lossEl.value:'play'})
    .then(async r=>{if(!r.ok) throw new Error('http');liveDirty=false;applyMeta(await r.json());})
    .catch(dropHint);
}
function markSetupDirty(){
  liveDirty=true;
  if(bandEl&&bandRow&&!bandRow.hidden) bandDirty=true;
  if(setupSave) setupSave.textContent='Save';
}
function saveSetup(){
  if(setupSave){setupSave.textContent='Saving…';setupSave.disabled=true;}
  const jobs=[];
  if(bandEl&&bandRow&&!bandRow.hidden) jobs.push(postBand());
  jobs.push(postLive());
  Promise.all(jobs).then(()=>{
    if(setupSave){setupSave.textContent='Saved';setupSave.disabled=false;}
    setStatus('Settings saved.');
  }).catch(()=>{
    if(setupSave){setupSave.textContent='Save';setupSave.disabled=false;}
    dropHint();
  });
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
  patchList.innerHTML='';
  patch.forEach((row,i)=>{
    const child=isChild(patch,i);
    const [g0,g1]=groupBounds(patch,i);
    const card=document.createElement('div');
    card.className='patch'+(child?' child':'')+(openSeg===i?' open':'');
    card.dataset.i=String(i);
    const title=patchTitle(row,child,totals);
    const locked=child?' disabled':'';
    const clocked=!!CLOCKED[row.chip];
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
    const outIdx=child?-1:outputIndex(patch,i);
    const testMode=outIdx>=0?(testModes[outIdx]||''):'';
    const testBtn=(mode,label)=>'<button type="button" data-test="'+mode+'"'+(testMode===mode?' class="on"':'')+(ledsOwned?' disabled':'')+'>'+label+'</button>';
    const testRow=child?'':'<div class="testrow">'+testBtn('rainbow','Rainbow')+testBtn('cycle','Cycle')+testBtn('ends','Ends')+'</div>';
    card.innerHTML=
      '<div class="patchhead"><b>'+escapeHtml(title)+'</b><div class="patchops">'+ops+
      '</div></div>'+testRow+'<div class="patchbody"><div class="patchgrid">'+
      '<div class="patchfield"><label class="lab">Protocol</label><select data-f="proto">'+
      '<option value="auto"'+(row.proto==='auto'?' selected':'')+'>Auto</option>'+
      '<option value="artnet"'+(row.proto==='artnet'?' selected':'')+'>Art-Net</option>'+
      '<option value="sacn"'+(row.proto==='sacn'?' selected':'')+'>sACN</option></select></div>'+
      '<div class="patchfield"><label class="lab">IC</label><select data-f="chip"'+locked+'>'+chipOpts(row.chip)+'</select></div>'+
      '<div class="patchfield"><label class="lab">Data GPIO</label>'+
      '<input data-f="data" type="number" min="0" max="'+patchCaps.gpio_max+'" value="'+row.data+'" inputmode="numeric"'+locked+'></div>'+
      '<div class="patchfield clkrow'+(clocked?' on':'')+'"><label class="lab">Clock GPIO</label>'+
      '<input data-f="clk" type="number" min="0" max="'+patchCaps.gpio_max+'" value="'+(row.clk==null?0:row.clk)+'" inputmode="numeric"'+locked+'></div>'+
      '<div class="patchfield'+(clocked?' span2':'')+'"><label class="lab">Pixels</label>'+
      '<input data-f="count" type="number" min="1" max="'+patchCaps.max_px+'" value="'+row.count+'" inputmode="numeric"></div>'+
      '<div class="patchfield"><label class="lab">Channels</label><div class="patchtogs">'+
      '<label class="tog"><input data-f="white" type="checkbox"'+(row.white?' checked':'')+'> White</label>'+
      '<label class="tog"><input data-f="cct" type="checkbox"'+(row.cct?' checked':'')+'> CCT</label></div></div>'+
      '<div class="patchfield"><label class="lab">Color order</label><select data-f="order">'+orderOpts(row.white,row.cct,row.order)+'</select></div>'+
      '<div class="patchfield"><label class="lab">Start universe</label>'+
      '<input data-f="uni" type="number" min="0" max="32767" value="'+row.uni+'" inputmode="numeric"></div>'+
      '<div class="patchfield"><label class="lab">Start channel</label>'+
      '<input data-f="ch" type="number" min="1" max="512" value="'+row.ch+'" inputmode="numeric"></div>'+
      '<p class="hint span2">'+uniHint(row.proto)+'</p>'+
      '<div class="patchfield span2"><label class="lab">Brightness</label><div class="brirow">'+
      '<input data-f="bri" type="range" min="0" max="255" value="'+row.bri+'">'+
      '<input data-f="brinum" type="number" min="0" max="255" value="'+row.bri+'" inputmode="numeric"></div></div>'+
      '<p class="briwarn span2'+(briWarnOn(row.bri)?' on':'')+'">'+briWarnText()+'</p>'+
      '<p class="readout span2">'+escapeHtml(uniReadout(row))+'</p></div></div>';
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
    });
    card.querySelectorAll('.testrow button').forEach(btn=>{
      btn.onclick=e=>{
        e.stopPropagation();
        if(ledsOwned||outIdx<0) return;
        const mode=btn.dataset.test;
        const cur=testModes[outIdx]||'';
        const next=cur===mode?'off':mode;
        testHold=Date.now()+2000;
        readPatchDom();
        postForm('/test',{i:String(outIdx),mode:next}).then(r=>{
          if(r.status===503){ testHold=0; setNote('Unavailable while live','err'); return; }
          if(!r.ok) throw new Error('http');
          testModes[outIdx]=next==='off'?'':next;
          testHold=Date.now()+2000;
          renderPatch();
        }).catch(()=>{ testHold=0; dropHint(); });
      };
    });
    card.querySelectorAll('input,select').forEach(el=>{
      const f=el.getAttribute('data-f');
      if(f==='bri'||f==='brinum'){
        el.oninput=()=>{
          const v=Math.max(0,Math.min(255,parseInt(el.value,10)||0));
          const range=card.querySelector('[data-f="bri"]');
          const num=card.querySelector('[data-f="brinum"]');
          if(range) range.value=String(v);
          if(num) num.value=String(v);
          const warn=card.querySelector('.briwarn');
          if(warn) warn.className='briwarn span2'+(briWarnOn(v)?' on':'');
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
          if(clk) clk.className='patchfield clkrow'+(CLOCKED[el.value]?' on':'');
          const cnt=card.querySelector('[data-f="count"]');
          const cntField=cnt&&cnt.closest('.patchfield');
          if(cntField){
            if(CLOCKED[el.value]) cntField.classList.add('span2');
            else cntField.classList.remove('span2');
          }
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
        refreshPatchTitles();
      };
    });
    patchList.appendChild(card);
  });
  if(mapAdd) mapAdd.disabled=patch.length>=patchCaps.max_seg;
}
function savePatch(){
  mapSave.textContent='Saving…';
  mapSave.disabled=true;
  const jobs=[];
  if(mapDirty) jobs.push(postMap());
  if(liveDirty) jobs.push(postLive());
  Promise.all(jobs).then(results=>{
    mapSave.textContent='Save';
    mapSave.disabled=false;
    if(results.some(v=>v===true)){
      markPatchSaving();
      return;
    }
    if(!mapDirty&&!liveDirty) setNote('');
  }).catch(e=>{
    mapSave.textContent='Save';
    mapSave.disabled=false;
    dropHint();
  });
}
function parsePlayN(){
  const t=foldernEl.value.trim();
  if(!/^\d+$/.test(t)) return 1;
  return Math.max(1,Math.min(99,parseInt(t,10)));
}
function wantsOverride(){return streamLive&&!playHold;}
function confirmOverride(){
  if(!wantsOverride()) return true;
  return window.confirm('A live stream is in progress. Override it and play this recorded show?');
}
function postPlay(src,path){
  if(!confirmOverride()) return Promise.resolve();
  const n=parsePlayN();
  foldernEl.value=String(n);
  playSrc=src||playSrc;
  playPath=path||playPath;
  const fields={
    src:playSrc,path:playPath,file_loop:fileloopEl.value,folder_rep:folderrepEl.value,n:String(n)
  };
  if(wantsOverride()) fields.override='1';
  return postForm('/play',fields).then(async r=>{
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
fpsEl.onchange=markSetupDirty;
bufEl.onchange=markSetupDirty;
parkEl.onchange=markSetupDirty;
if(takeoverEl) takeoverEl.onchange=markSetupDirty;
if(unisyncEl) unisyncEl.onchange=markSetupDirty;
if(lossEl) lossEl.onchange=markSetupDirty;
if(bandEl) bandEl.onchange=()=>{
  markSetupDirty();
  renderNets();
  const ssid=ssidEl.value;
  const n=mergeNets(lastNets).find(x=>x.ssid===ssid);
  if(n){
    const t=pickTwin(n);
    selBssid=t&&t.bssid?t.bssid:'';
    selCh=t&&t.ch?t.ch:0;
  }
};
if(ssidEl) ssidEl.oninput=()=>{selBssid='';selCh=0;paintNetClasses();};
const passEye=document.getElementById('passEye');
if(passEye) passEye.onclick=()=>{
  const show=passEl.type==='password';
  passEl.type=show?'text':'password';
  passEye.setAttribute('aria-label',show?'Hide password':'Show password');
  passEye.title=show?'Hide password':'Show password';
};
if(setupSave) setupSave.onclick=saveSetup;
['snRole','snSsid','snPass','snCh'].forEach(id=>{const e=document.getElementById(id);if(e){e.oninput=e.onchange=()=>{snDirty=true;snShow();};}});
document.getElementById('snSave').onclick=saveShowNet;
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
function applyPlayPost(r){return r.json().then(s=>{if(!r.ok) throw new Error('http');playDirty=false;applyMeta(s);});}
document.getElementById('play').onclick=()=>{
  const same=playSrc===cfgSrc&&(playSrc==='root'||playPath===cfgPath);
  if(same&&playPaused){
    if(!confirmOverride()) return;
    const fields={action:'resume'};
    if(wantsOverride()) fields.override='1';
    postForm('/play',fields).then(async r=>applyPlayPost(r)).catch(dropHint);
    return;
  }
  postPlay();
};
document.getElementById('setStartup').onclick=()=>{
  const n=parsePlayN();
  foldernEl.value=String(n);
  postForm('/play',{
    action:'startup',src:playSrc,path:playPath,
    file_loop:fileloopEl.value,folder_rep:folderrepEl.value,n:String(n)
  }).then(async r=>{
    if(!r.ok) throw new Error('http');
    applyMeta(await r.json());
  }).catch(dropHint);
};
document.getElementById('clearStartup').onclick=()=>{
  postForm('/play',{action:'startup',src:'none'}).then(async r=>{
    if(!r.ok) throw new Error('http');
    applyMeta(await r.json());
  }).catch(dropHint);
};
document.getElementById('toStream').onclick=()=>postForm('/play',{action:'live'}).then(async r=>applyPlayPost(r)).catch(dropHint);
document.getElementById('pause').onclick=()=>postForm('/play',{action:'pause'}).then(async r=>applyPlayPost(r)).catch(dropHint);
document.getElementById('stop').onclick=()=>postForm('/play',{src:'stop'}).then(async r=>applyPlayPost(r)).catch(dropHint);
document.getElementById('prev').onclick=()=>{const t=adjacent(-1);if(t){playDirty=true;playSrc='file';playPath=t;playSel=new Set([rowKey('file',t)]);playAnchor=rowKey('file',t);markPlaySel();showPlayOpts();postPlay('file',t);}};
document.getElementById('next').onclick=()=>{const t=adjacent(1);if(t){playDirty=true;playSrc='file';playPath=t;playSel=new Set([rowKey('file',t)]);playAnchor=rowKey('file',t);markPlaySel();showPlayOpts();postPlay('file',t);}};
document.getElementById('dist').onclick=()=>{
  const paths=[];
  playSel.forEach(k=>{
    const i=k.indexOf('\t');
    if(k.slice(0,i)==='file') paths.push(k.slice(i+1));
  });
  if(paths.length!==1){setNote('Select one full show to distribute.','err');return;}
  if(!window.confirm('Slice '+fileTitle(paths[0])+' for every node on the network and send each its part? Their playback stops while it copies.')) return;
  postForm('/distribute',{path:paths[0]}).then(async r=>{
    const s=await r.json().catch(()=>({}));
    if(!r.ok){setNote(s.error==='no peers'?'No other nodes heard on the network.':(s.error||'Distribute failed'),'err');return;}
    setNote('Distributing…','ok');
  }).catch(dropHint);
};
let streamOn=false;
document.getElementById('strm').onclick=()=>{
  if(streamOn){
    postForm('/stream',{on:'0'}).then(async r=>{applyMeta(await r.json());setNote('Stream stopped.','ok');}).catch(dropHint);
    return;
  }
  const paths=[];
  playSel.forEach(k=>{
    const i=k.indexOf('\t');
    if(k.slice(0,i)==='file') paths.push(k.slice(i+1));
  });
  if(paths.length!==1){setNote('Select one full show to stream.','err');return;}
  postForm('/stream',{path:paths[0],on:'1'}).then(async r=>{
    const s=await r.json().catch(()=>({}));
    if(!r.ok){setNote(s.error==='no peers'?'No other nodes heard on the network.':(s.error||'Stream failed'),'err');return;}
    setNote('Streaming '+fileTitle(paths[0])+' to the group.','ok');
    applyMeta(s);
  }).catch(dropHint);
};
function applyStream(s){
  const st=s.stream;
  if(!st) return;
  streamOn=!!st.on;
  const b=document.getElementById('strm');
  b.classList.toggle('on',streamOn);
  b.title=streamOn?'Streaming to '+st.peers+' node(s). Press to stop.':'Stream: play this full show here and send every node its part live (no SD needed on them)';
}
function applyDist(s){
  applyStream(s);
  const d=s.dist;
  if(!d||d.state==='idle') return;
  if(d.state==='running'){
    const pct=d.total?Math.round(100*d.sent/d.total):0;
    setNote('Distributing to '+(d.peer||'…')+' ('+(d.i+1)+'/'+d.n+') '+pct+'%','ok');
  }else if(d.state==='done'&&noteEl.textContent.indexOf('Distributing')===0){
    setNote('Distributed: '+d.msg,'ok');
  }else if(d.state==='error'&&noteEl.textContent.indexOf('Distributing')===0){
    setNote('Distribute failed: '+d.msg,'err');
  }
}
document.getElementById('del').onclick=()=>{
  const paths=[];
  playSel.forEach(k=>{
    const i=k.indexOf('\t');
    if(k.slice(0,i)==='file') paths.push(k.slice(i+1));
  });
  if(!paths.length) return;
  if(!window.confirm('Delete '+paths.length+' look'+(paths.length>1?'s':'')+' from this node?')) return;
  postForm('/delete',{path:paths}).then(async r=>{
    const s=await r.json();
    if(!r.ok){setNote(s.error||'Delete failed','err');return;}
    playDirty=false;
    setNote('');
    applyMeta(s);
  }).catch(dropHint);
};
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
renderPatch();
(async()=>{
  try{
    const s=await jget('/api/stats');
    applyStats(s);
    showStatus(s);
    if(s.ip) showTab('live');
    else showTab('setup');
    if(patchSaveFlagOn()) finishPatchSave();
    poll();
  }catch(e){dropHint();}
})();
</script>
</body>
</html>
)WIFIHTML";
