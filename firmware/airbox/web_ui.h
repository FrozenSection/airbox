// web_ui.h — self-contained HTML/CSS/JS served from PROGMEM (flash).
// No external CDN: everything needed renders on a network with no internet.
// Two pages:
//   PORTAL_HTML — captive-portal WiFi provisioning (AP mode)
//   INDEX_HTML  — live dashboard / diagnostics / settings (STA mode)
//
// Charts are hand-drawn on <canvas>; no charting library. Keep this lean —
// it streams straight from flash via request->send_P(), costing ~0 heap.

#ifndef WEB_UI_H
#define WEB_UI_H

#include <pgmspace.h>

// =====================================================================
// Captive portal — WiFi provisioning
// =====================================================================
const char PORTAL_HTML[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AirBox — WiFi Setup</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#11151c;color:#e6edf3;margin:0;padding:18px;max-width:520px;margin:0 auto}
h1{font-size:1.3rem;margin:.2rem 0 1rem}
.card{background:#1b2230;border:1px solid #2a3446;border-radius:12px;padding:16px;margin-bottom:14px}
label{display:block;font-size:.85rem;color:#9fb0c3;margin:10px 0 4px}
select,input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #33405a;background:#0e1320;color:#e6edf3;font-size:1rem}
button{width:100%;padding:12px;border:0;border-radius:9px;background:#2f6df6;color:#fff;font-size:1rem;font-weight:600;margin-top:14px;cursor:pointer}
button.alt{background:#33405a;margin-top:8px}
.row{display:flex;gap:8px;align-items:center}
.row input[type=checkbox]{width:auto}
small{color:#8493a8;display:block;margin-top:8px;line-height:1.4}
#msg{margin-top:10px;font-size:.95rem}
</style></head><body>
<h1>📡 AirBox — WiFi Setup</h1>
<div class="card">
  <label for="net">Your WiFi network</label>
  <select id="net"><option>Scanning…</option></select>
  <button class="alt" id="rescan" type="button">Rescan networks</button>
  <label for="pw">Password</label>
  <input id="pw" type="password" placeholder="network password" autocomplete="off">
  <div class="row" style="margin-top:8px"><input id="show" type="checkbox"><label for="show" style="margin:0">Show password</label></div>
  <button id="go" type="button">Connect</button>
  <div id="msg"></div>
  <small>The device will reboot and join your network. To change networks
  later, hold the BOOT button for 3 seconds, or use “Reconfigure WiFi” on the
  dashboard. Calibration data is preserved across a WiFi reset.</small>
</div>
<script>
var net=document.getElementById('net'),pw=document.getElementById('pw'),
    go=document.getElementById('go'),msg=document.getElementById('msg'),
    rescan=document.getElementById('rescan');
document.getElementById('show').onchange=function(e){pw.type=e.target.checked?'text':'password'};
function fill(list){
  if(!list.length){net.innerHTML='<option value="">No networks found</option>';return;}
  list.sort(function(a,b){return b.rssi-a.rssi});
  net.innerHTML=list.map(function(n){
    var lock=n.enc?'🔒':'';var bars=n.rssi>-60?'▂▄▆█':n.rssi>-72?'▂▄▆':'▂▄';
    return '<option value="'+n.ssid.replace(/"/g,'&quot;')+'">'+n.ssid+' '+lock+' '+bars+'</option>';
  }).join('');
}
function load(){fetch('/scan').then(function(r){return r.json()}).then(function(d){
  if(d.scanning){setTimeout(load,1200);net.innerHTML='<option>Scanning…</option>';return;}
  fill(d.networks||[]);
}).catch(function(){setTimeout(load,1500)});}
rescan.onclick=function(){net.innerHTML='<option>Scanning…</option>';fetch('/rescan').then(load)};
go.onclick=function(){
  var ssid=net.value;if(!ssid){msg.textContent='Pick a network first.';return;}
  go.disabled=true;msg.textContent='Saving…';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pw.value)})
  .then(function(){msg.innerHTML='✅ Saved. The device is rebooting and joining <b>'+ssid+'</b>.<br>This page will stop responding — reconnect to your normal WiFi.';})
  .catch(function(){msg.textContent='Saved (device rebooting).';});
};
load();
</script></body></html>)=====";

// =====================================================================
// Dashboard / Diagnostics / Settings
// =====================================================================
const char INDEX_HTML[] PROGMEM = R"=====(<!DOCTYPE html>
<!--
  AirBox — self-contained dashboard for web_ui.h (INDEX_HTML).
  No external CDN / fonts / libraries. Vanilla JS. Canvas charts.
  Polls /api/data (4s) + /api/history (60s); posts /api/settings.
  Previews standalone via a mock-data fallback when the API is absent.
-->
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>AirBox</title>
<style>
:root{
  color-scheme:dark;
  --bg:oklch(0.185 0.012 70); --ink:#f4efe8;
  --dim:rgba(244,239,232,0.5); --faint:rgba(244,239,232,0.38);
  --card:rgba(255,255,255,0.025); --card2:rgba(255,255,255,0.04);
  --line:rgba(255,255,255,0.07); --line2:rgba(255,255,255,0.1);
  --t:oklch(0.72 0.13 38); --h:oklch(0.70 0.11 243); --p:oklch(0.68 0.12 300); --i:oklch(0.72 0.12 150);
  --good:oklch(0.78 0.10 168); --fair:oklch(0.83 0.11 82); --mod:oklch(0.80 0.12 62); --poor:oklch(0.70 0.14 30);
  --accent:oklch(0.64 0.12 245);
}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--ink);
  font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
body{position:relative;overflow-x:hidden}
#glow{position:fixed;top:-260px;left:50%;transform:translateX(-50%);width:1100px;height:600px;
  border-radius:50%;filter:blur(8px);opacity:.10;pointer-events:none;z-index:0;
  background:radial-gradient(closest-side,var(--cc,var(--good)),transparent)}
.wrap{position:relative;z-index:1;max-width:1240px;margin:0 auto;padding:24px 22px 48px}

/* header */
header{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;margin-bottom:18px}
.brand .name{font-size:18px;font-weight:700;letter-spacing:-.01em}
.brand a{font-size:12px;color:var(--faint);text-decoration:none}
.brand a:hover{color:var(--dim)}
.tabs{display:flex;gap:4px;padding:4px;border-radius:14px;background:var(--card2);border:1px solid var(--line)}
.tabs button{appearance:none;border:0;cursor:pointer;font:inherit;padding:9px 20px;border-radius:10px;
  font-size:14px;font-weight:500;color:var(--dim);background:transparent;transition:.12s}
.tabs button.on{color:var(--ink);font-weight:600;background:rgba(255,255,255,.07);box-shadow:inset 0 1px 0 rgba(255,255,255,.06)}
.meta{text-align:right;font-size:13px;color:var(--dim);white-space:nowrap}
.meta .dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--good);
  box-shadow:0 0 8px var(--good);margin-right:7px;vertical-align:middle}
.meta .dot.bad{background:var(--poor);box-shadow:0 0 8px var(--poor)}
.meta .sub{font-size:11.5px;color:var(--faint);margin-top:3px}

/* generic */
.uc{font-size:13px;letter-spacing:.14em;text-transform:uppercase;color:var(--dim);font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:22px 24px}
.val{font-weight:600;letter-spacing:-.02em;line-height:1;font-variant-numeric:tabular-nums}
.unit{color:var(--dim);font-weight:500}
.note{font-size:14.5px;line-height:1.5;color:rgba(244,239,232,.62);text-wrap:pretty}
.axis{display:flex;justify-content:space-between;font-size:11px;color:var(--faint);margin-top:4px}
canvas{display:block;width:100%}
.hide{display:none!important}

/* range bar */
.bar{position:relative;height:8px;border-radius:8px;background:rgba(255,255,255,.08);width:100%}
.bar .band{position:absolute;top:0;bottom:0;border-radius:8px;opacity:.28}
.bar .mark{position:absolute;top:50%;width:14px;height:14px;margin:-7px 0 0 -7px;border-radius:50%;
  box-shadow:0 0 0 3px rgba(22,21,19,.9)}
.barcap{display:flex;justify-content:space-between;font-size:12px;color:var(--faint);margin-top:8px}

/* dashboard */
.hero{border-radius:26px;padding:28px 32px;border:1px solid var(--line2);
  background:linear-gradient(160deg,var(--cc-wash,rgba(255,255,255,.04)),rgba(255,255,255,.012))}
.herogrid{display:grid;grid-template-columns:330px 1fr 1px 1fr;gap:40px;align-items:stretch}
.verdict{font-size:48px;font-weight:700;letter-spacing:-.03em;line-height:1.02;margin:0 0 12px;color:var(--cc,var(--good))}
.divider{background:var(--line2)}
.metric .top{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
.metric .big{display:flex;align-items:baseline;gap:6px;margin-bottom:14px}
.metric .big .val{font-size:60px}
.metric .big .unit{font-size:21px}
.support{display:grid;grid-template-columns:1fr 1fr 1.05fr;gap:18px;margin-top:24px}
.pill{display:inline-flex;align-items:center;gap:7px;font-size:12.5px;font-weight:600;white-space:nowrap}
.pill .d{width:8px;height:8px;border-radius:50%}
.cal{display:inline-flex;align-items:center;gap:9px;padding:5px 10px 5px 8px;border-radius:999px;
  background:var(--card2);border:1px solid var(--line2);font-size:12px;color:var(--dim);margin-top:12px;width:fit-content}
.cal .segs{display:flex;gap:3px}
.cal .segs i{width:14px;height:5px;border-radius:3px;background:rgba(255,255,255,.13)}
.cal.done{background:color-mix(in oklch,var(--good) 12%,transparent);border-color:color-mix(in oklch,var(--good) 40%,transparent)}
.support .scard{display:flex;flex-direction:column;gap:14px}
.support .srow{display:flex;align-items:baseline;justify-content:space-between}
.support .srow .val{font-size:40px}.support .srow .unit{font-size:15px}

/* diagnostics + settings */
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:18px;align-items:start}
.panel{display:flex;flex-direction:column;gap:0}
.panel h3{margin:0 0 4px}
.drow{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:13px 0;border-bottom:1px solid var(--line)}
.drow:last-child{border-bottom:0}
.drow .k{font-size:14.5px;color:var(--dim);white-space:nowrap}
.drow .v{font-size:14.5px;font-weight:500;color:var(--ink);font-variant-numeric:tabular-nums;text-align:right}
.bars{display:inline-flex;align-items:flex-end;gap:2.5px;height:14px;margin-right:9px;vertical-align:middle}
.bars i{width:4px;border-radius:1px;background:rgba(255,255,255,.18)}
.pnote{font-size:12.5px;line-height:1.5;color:var(--faint);margin-top:14px;text-wrap:pretty}

/* forms */
.field{display:flex;flex-direction:column;gap:8px;margin-top:16px}
.field:first-of-type{margin-top:0}
label{font-size:13.5px;color:var(--ink);font-weight:500}
label .hint{color:var(--faint);font-weight:400}
input,select{width:100%;padding:12px 14px;border-radius:11px;background:var(--card2);
  border:1px solid var(--line2);color:var(--ink);font-size:15px;font-family:inherit;outline:none;transition:.12s}
select{appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12' fill='none' stroke='%23bbb' stroke-width='1.8' stroke-linecap='round'%3E%3Cpath d='M2 4l4 4 4-4'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 14px center;padding-right:36px;cursor:pointer}
input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px color-mix(in oklch,var(--accent) 28%,transparent)}
input::placeholder{color:rgba(244,239,232,.32)}
.minmax{display:flex;align-items:center;gap:10px}
.minmax span{color:var(--dim);font-size:13.5px;white-space:nowrap}
.switch{width:46px;height:27px;border-radius:14px;border:0;cursor:pointer;padding:0;position:relative;
  background:rgba(255,255,255,.14);transition:.15s;flex:0 0 auto}
.switch.on{background:var(--accent)}
.switch i{position:absolute;top:3px;left:3px;width:21px;height:21px;border-radius:50%;background:#fff;
  transition:.15s;box-shadow:0 1px 3px rgba(0,0,0,.3)}
.switch.on i{left:22px}
.sub{padding-left:16px;border-left:2px solid var(--line2);margin-top:14px;transition:opacity .15s}
.sub.off{opacity:.4;pointer-events:none}
.btn{appearance:none;font:inherit;padding:12px 18px;border-radius:11px;font-size:14.5px;font-weight:600;
  cursor:pointer;white-space:nowrap;border:1px solid var(--line2);background:var(--card2);color:var(--ink)}
.btn.primary{border:0;background:var(--accent);color:#fff}
.btn.amber{border-color:color-mix(in oklch,var(--mod) 45%,transparent);background:color-mix(in oklch,var(--mod) 12%,transparent);color:var(--mod)}
.btn.danger{border-color:color-mix(in oklch,var(--poor) 45%,transparent);background:color-mix(in oklch,var(--poor) 12%,transparent);color:var(--poor)}
.row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.btns{display:flex;flex-wrap:wrap;gap:10px}
.msg{font-size:13.5px;font-weight:500;color:var(--good)}

/* responsive */
@media(max-width:900px){
  .herogrid{grid-template-columns:1fr;gap:22px}
  .divider{display:none}
  .support{grid-template-columns:1fr}
  .grid2{grid-template-columns:1fr}
  .tabs{order:3;width:100%}.tabs button{flex:1}
  .meta{text-align:left}
}
</style>
</head>
<body>
<div id="glow"></div>
<div class="wrap">

  <header>
    <div class="brand">
      <div class="name" id="hName">AirBox</div>
      <a href="https://github.com/FrozenSection/airbox" target="_blank" rel="noopener">github.com/FrozenSection/airbox</a>
    </div>
    <div class="tabs">
      <button class="on" data-tab="dash">Dashboard</button>
      <button data-tab="diag">Diagnostics</button>
      <button data-tab="set">Settings</button>
    </div>
    <div class="meta">
      <div><span class="dot" id="conn"></span><span id="stamp">—</span></div>
      <div class="sub" id="hMeta">airbox.local · v—</div>
    </div>
  </header>

  <!-- ============ DASHBOARD ============ -->
  <section id="dash">
    <div class="hero">
      <div class="herogrid">
        <div>
          <div class="verdict" id="vWord">—</div>
          <div class="note" id="vNote"></div>
        </div>
        <div class="metric">
          <div class="top"><span class="uc">Temperature</span></div>
          <div class="big"><span class="val" id="tVal">—</span><span class="unit" id="tUnit">°F</span></div>
          <canvas id="cT" height="44"></canvas><div class="axis"><span>−24h</span><span>now</span></div>
          <div style="margin-top:14px"><div class="bar" id="tBar"><div class="band"></div><div class="mark"></div></div>
            <div class="barcap"><span id="tIdeal">ideal</span><span id="tDelta"></span></div></div>
        </div>
        <div class="divider"></div>
        <div class="metric">
          <div class="top"><span class="uc">Humidity</span></div>
          <div class="big"><span class="val" id="hVal">—</span><span class="unit">%</span></div>
          <canvas id="cH" height="44"></canvas><div class="axis"><span>−24h</span><span>now</span></div>
          <div style="margin-top:14px"><div class="bar" id="hBar"><div class="band"></div><div class="mark"></div></div>
            <div class="barcap"><span id="hIdeal">ideal</span><span id="hDelta"></span></div></div>
        </div>
      </div>
    </div>

    <div class="support">
      <div class="card scard">
        <div class="top"><span class="uc">Pressure</span></div>
        <div class="srow"><div class="big" style="margin:0"><span class="val" id="pVal">—</span><span class="unit">hPa</span></div></div>
        <canvas id="cP" height="38"></canvas><div class="axis"><span>−24h</span><span>now</span></div>
      </div>
      <div class="card scard">
        <div class="top"><span class="uc">Air Quality</span><span class="pill" id="iPill"></span></div>
        <div class="srow"><div class="big" style="margin:0"><span class="val" id="iVal">—</span><span class="unit">IAQ</span></div></div>
        <canvas id="cI" height="38"></canvas><div class="axis"><span>−24h</span><span>now</span></div>
        <div class="cal" id="iCal"></div>
      </div>
      <div class="card scard" style="justify-content:center;gap:9px">
        <span class="uc">Air quality note</span>
        <span class="note" id="iNote"></span>
      </div>
    </div>
  </section>

  <!-- ============ DIAGNOSTICS ============ -->
  <section id="diag" class="hide">
    <div class="grid2">
      <div class="card panel"><h3 class="uc">Sensors</h3><div id="dgSens"></div>
        <div class="pnote">HDC3022 is the trusted T/RH source. Calibration 0–3 is the BME688’s BSEC self-calibration state (3 = fully calibrated, reached over 24–48 h).</div></div>
      <div class="card panel"><h3 class="uc">Air quality detail (BME688 / BSEC)</h3><div id="dgAq"></div>
        <div class="pnote">Raw BME T/RH read high from gas-heater self-heating. eCO₂ and bVOC are BSEC estimates derived from the same gas signal as IAQ.</div></div>
      <div class="card panel"><h3 class="uc">Network</h3><div id="dgNet"></div></div>
      <div class="card panel"><h3 class="uc">System</h3><div id="dgSys"></div></div>
    </div>
    <div class="card panel" style="margin-top:18px"><h3 class="uc">Export data (CSV)</h3>
      <div class="note" style="margin:8px 0 16px">Downloads the last 7 days of readings at 5-minute spacing, stored on the device and surviving reboots. The dashboard chart shows the most recent 24 h.</div>
      <a class="btn" id="csvBtn" href="/api/history.csv" style="text-decoration:none;width:fit-content">↓ Download CSV · 7 days</a>
      <div class="pnote">Columns: timestamp (device local time), temperature, humidity, pressure, IAQ.</div>
    </div>
  </section>

  <!-- ============ SETTINGS ============ -->
  <section id="set" class="hide">
    <div class="grid2">
      <div style="display:flex;flex-direction:column;gap:18px">
        <div class="card panel"><h3 class="uc">Device</h3>
          <div class="field"><label>Device name</label><input id="sName"/></div>
          <div class="field"><label>Temperature unit</label>
            <select id="sUnit"><option value="F">Fahrenheit (°F)</option><option value="C">Celsius (°C)</option></select></div>
          <div class="field"><label>mDNS hostname (.local)</label><input id="sHost"/></div>
          <div class="field"><label>Time zone <span class="hint">— timestamps &amp; clock; DST automatic</span></label>
            <select id="sTz"></select></div>
        </div>
        <div class="card panel"><h3 class="uc">Comfort targets</h3>
          <div class="pnote" style="margin:6px 0 4px">The ideal bands behind the dashboard verdict and range bars. Applies on save.</div>
          <div class="field"><label>Temperature (<span id="cuLbl">°F</span>)</label>
            <div class="minmax"><input type="number" id="sTmin"/><span>to</span><input type="number" id="sTmax"/><span id="cuLbl2">°F</span></div></div>
          <div class="field"><label>Relative humidity (%)</label>
            <div class="minmax"><input type="number" id="sHmin"/><span>to</span><input type="number" id="sHmax"/><span>%</span></div></div>
        </div>
        <div class="card panel"><h3 class="uc">Security</h3>
          <div class="field"><label>Admin password <span class="hint">— blank = keep current</span></label>
            <input type="password" id="sPass" placeholder="OTA + Settings auth" autocomplete="off"/></div>
        </div>
        <div class="row"><button class="btn primary" id="saveBtn">Save settings</button>
          <button class="btn" id="discardBtn">Discard changes</button><span class="msg" id="sMsg"></span></div>
      </div>
      <div style="display:flex;flex-direction:column;gap:18px">
        <div class="card panel"><h3 class="uc">Display</h3>
          <div class="field"><label>Brightness</label>
            <select id="sBright"><option value="8">Low (longest OLED life)</option><option value="64">Medium</option><option value="160">High</option><option value="255">Max</option></select></div>
          <div class="field"><div class="row" style="justify-content:space-between">
            <div><div style="font-weight:600">Night mode</div><div class="hint" style="font-size:13px;color:var(--faint)">Blank or dim the screen on a schedule.</div></div>
            <button class="switch" id="sNight"><i></i></button></div>
            <div class="sub off" id="nightSub">
              <div class="field"><label>During night hours</label>
                <select id="sNMode"><option value="0">Turn screen off (blank)</option><option value="1">Dim screen (very low)</option></select></div>
              <div class="row" style="gap:14px"><div class="field" style="flex:1;margin-top:0"><label>Starts at (0–23)</label><input type="number" min="0" max="23" id="sNStart"/></div>
                <div class="field" style="flex:1;margin-top:0"><label>Ends at (hour)</label><input type="number" min="0" max="23" id="sNEnd"/></div></div>
            </div>
          </div>
        </div>
        <div class="card panel"><h3 class="uc">Maintenance</h3>
          <div class="btns"><a class="btn" href="/update" style="text-decoration:none">Firmware update (OTA)</a>
            <button class="btn amber" id="recalBtn">Recalibrate air sensor</button>
            <button class="btn" id="recfgBtn">Reconfigure WiFi</button>
            <button class="btn danger" id="restartBtn">Restart device</button></div>
          <div class="pnote">Recalibrate clears the BSEC baseline — IAQ accuracy drops to 0 and re-learns over 24–48 h. Reconfigure WiFi reboots into the setup portal (you’ll rejoin “AirBox-Setup” to pick a new network).</div>
        </div>
        <div class="card panel"><h3 class="uc">About this device</h3>
          <div class="field" style="gap:3px;margin-top:14px"><span style="color:var(--faint);font-size:12px">Model</span><span style="font-weight:500;font-size:14px">QT Py ESP32-S3</span></div>
          <div class="field" style="gap:3px"><span style="color:var(--faint);font-size:12px">Sensors</span><span style="font-weight:500;font-size:14px">BME688 · HDC3022</span></div>
          <div class="field" style="gap:3px"><span style="color:var(--faint);font-size:12px">Firmware</span><span style="font-weight:500;font-size:14px" id="aFw">v—</span></div>
        </div>
      </div>
    </div>
  </section>

</div>
<script>
/* AirBox dashboard logic — vanilla, no deps. Polls /api/data + /api/history,
   posts /api/settings. Falls back to MOCK data when the API isn't reachable
   (so this file previews in a plain browser). */
(function(){
'use strict';
var $=function(id){return document.getElementById(id);};
var SIG={t:'oklch(0.72 0.13 38)',h:'oklch(0.70 0.11 243)',p:'oklch(0.68 0.12 300)',i:'oklch(0.72 0.12 150)'};
var ST={good:'oklch(0.78 0.10 168)',fair:'oklch(0.83 0.11 82)',mod:'oklch(0.80 0.12 62)',poor:'oklch(0.70 0.14 30)'};
var CC={ 'cold|dry':'oklch(0.70 0.12 250)','cold|ok':'oklch(0.81 0.07 230)','cold|humid':'oklch(0.70 0.12 250)',
  'ok|dry':'oklch(0.82 0.11 85)','ok|ok':'oklch(0.78 0.10 168)','ok|humid':'oklch(0.82 0.11 85)',
  'warm|ok':'oklch(0.75 0.14 55)','warm|dry':'oklch(0.69 0.16 28)','warm|humid':'oklch(0.69 0.16 28)' };
var MX={ 'ok|ok':['Comfortable','Temperature and humidity are both in your ideal range.'],
  'cold|ok':['Cool','A little cooler than your target — a touch of warmth would feel better.'],
  'warm|ok':['Warm','A little warmer than your target — a touch of cooling would feel better.'],
  'ok|dry':['Dry','Comfortable temperature, but the air is dry — a humidifier would help.'],
  'ok|humid':['Humid','Comfortable temperature, but the air is humid — ventilation would help.'],
  'cold|dry':['Cold & dry','Cool with dry air. Warm the room up and add a little moisture.'],
  'warm|dry':['Warm & dry','Warm with dry air. Cool the room down and add a little moisture.'],
  'cold|humid':['Cold & damp','Cool and humid — the room can feel clammy. Warm it up and ventilate.'],
  'warm|humid':['Warm & humid','Warm and humid — the room can feel muggy. Cool it down and ventilate.'] };
var TZ=['UTC','Eastern (New York)','Central (Chicago)','Mountain (Denver)','Arizona (no DST)','Pacific (Los Angeles)','Alaska (Anchorage)','Hawaii (no DST)','UK (London)','Central Europe','India (Kolkata)','Japan (Tokyo)','Sydney'];

function f2c(f){return (f-32)*5/9;} function c2f(c){return c*9/5+32;}
function fmt(v,d){return (v==null||isNaN(v))?'—':Number(v).toFixed(d==null?1:d);}
function tcat(v,c){return v<c.tmin?'cold':(v>c.tmax?'warm':'ok');}
function hcat(v,c){return v<c.hmin?'dry':(v>c.hmax?'humid':'ok');}
function comfort(t,h,c){var k=tcat(t,c)+'|'+hcat(h,c);var m=MX[k]||MX['ok|ok'];return {word:m[0],note:m[1],color:CC[k]||CC['ok|ok']};}
function iaqStat(v){if(v<=50)return['Good',ST.good];if(v<=100)return['Fair',ST.fair];if(v<=150)return['Moderate',ST.mod];return['Poor',ST.poor];}
function iaqInfo(acc,v){
  if(acc===0)return{label:'unreliable',done:false,body:'The BME688 just powered on — IAQ isn’t reliable yet. It warms up for a few minutes, then keeps learning the room over 24–48 h.'};
  if(acc===2)return{label:'medium',done:false,body:'IAQ is moderate ('+v+'). The sensor is partway calibrated — readings are usable and still sharpening.'};
  if(acc>=3)return{label:'high',done:true,body:'Air quality is moderate ('+v+') and the sensor is fully calibrated. Levels like this usually mean cooking, cleaning, or low ventilation — a little fresh air brings it down.'};
  return{label:'low',done:false,body:'IAQ is moderate ('+v+'). The BME688 is still self-calibrating — accuracy is low and improves over the first 24–48 h.'};
}
function ago(s){if(s==null)return '—';if(s<90)return s+'s';if(s<5400)return Math.round(s/60)+'m';return Math.round(s/3600)+'h';}

/* ---- mock fallback so the file renders standalone ---- */
var MOCK={name:'AirBox',hostname:'airbox',unit:'F',fw:'1.2.5',brightness:64,night_en:false,night_start:23,night_end:7,night_mode:0,tz:1,
  temp:71.4,rh:49,pressure:995.0,iaq:130,iaq_acc:3,bme_temp:72.7,bme_rh:45,eco2:1283,bvoc:2.95,
  hdc_ok:true,bme_ok:true,rssi:-62,ssid:'17-55',ip:'192.168.4.121',uptime:14400,heap:102400,reset_reason:'Software',hdc_age:1,bme_age:1,
  comfort:{tmin:68,tmax:76,hmin:40,hmax:60}};
var MH={interval_s:300,unit:'F',
  t:[68.2,68.5,68.4,68.7,69,69.3,69.1,69.5,69.9,70.2,70,70.4,70.7,71,70.8,70.9,68,66.1,69.6,70.9,71.2,71.4],
  rh:[52.1,52.4,52.7,52.2,51.6,51,50.4,50.8,51.3,50.6,49.9,49.4,49.8,52,50.6,49.7,48.9,48.4,47.9,48.6,48.9,49],
  p:[992.9,992.8,993,993.1,993,993.2,993.4,993.3,993.6,993.9,994.1,994,994.3,994.6,994.8,994.7,994.9,995,994.8,994.9,995.1,995],
  iaq:[96,98,97,99,101,104,108,112,118,121,119,123,127,131,129,130,72,49.8,118,128,132,130]};

function getJSON(url){return fetch(url,{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json();});}

var DATA=null, HIST=null;

/* ---- canvas chart (ambient sparkline: area + rounded line + end dot) ---- */
function chart(id,arr,color,minSpan){
  var c=$(id); if(!c)return; var w=c.clientWidth,h=c.clientHeight;
  if(!w||!h)return; var dpr=window.devicePixelRatio||1; c.width=w*dpr; c.height=h*dpr;
  var x=c.getContext('2d'); x.scale(dpr,dpr); x.clearRect(0,0,w,h);
  var v=(arr||[]).filter(function(p){return p!=null&&!isNaN(p);});
  if(v.length<2){x.fillStyle='rgba(244,239,232,.4)';x.font='11px system-ui';x.fillText('collecting…',2,h/2);return;}
  var dmn=Math.min.apply(null,v),dmx=Math.max.apply(null,v),mid=(dmn+dmx)/2,span=Math.max(dmx-dmn,minSpan||1);
  var lo=mid-span/2-span*0.18,hi=mid+span/2+span*0.18,rng=(hi-lo)||1,n=arr.length,pad=5;
  function X(i){return (n<2)?0:(i/(n-1))*w;} function Y(p){return pad+(h-2*pad)*(1-(p-lo)/rng);}
  // area
  var grad=x.createLinearGradient(0,0,0,h); grad.addColorStop(0,color); grad.addColorStop(1,'transparent');
  x.globalAlpha=0.15; x.fillStyle=grad; x.beginPath(); var started=false,lx=0,ly=0;
  for(var i=0;i<n;i++){var p=arr[i]; if(p==null||isNaN(p)){continue;} var px=X(i),py=Y(p);
    if(!started){x.moveTo(px,py);started=true;}else x.lineTo(px,py); lx=px;ly=py;}
  if(started){x.lineTo(lx,h);x.lineTo(X(0),h);x.closePath();x.fill();}
  x.globalAlpha=1;
  // line
  x.strokeStyle=color;x.lineWidth=2.4;x.lineJoin='round';x.lineCap='round';x.beginPath();started=false;
  for(var j=0;j<n;j++){var q=arr[j]; if(q==null||isNaN(q)){started=false;continue;} var qx=X(j),qy=Y(q);
    if(!started){x.moveTo(qx,qy);started=true;}else x.lineTo(qx,qy); lx=qx;ly=qy;}
  x.stroke();
  x.fillStyle=color;x.beginPath();x.arc(lx,ly,3,0,6.2832);x.fill();
}
function redraw(){ if(!HIST)return;
  chart('cT',HIST.t,SIG.t,2); chart('cH',HIST.rh,SIG.h,6); chart('cP',HIST.p,SIG.p,4); chart('cI',HIST.iaq,SIG.i,25);
}

/* ---- range bar ---- */
function rangeBar(barId,abs,ideal,value,color){
  var el=$(barId); var band=el.querySelector('.band'), mark=el.querySelector('.mark');
  var sp=abs[1]-abs[0], pc=function(v){return Math.max(0,Math.min(100,(v-abs[0])/sp*100));};
  var l=pc(ideal[0]),r=pc(ideal[1]),m=pc(value);
  band.style.left=l+'%'; band.style.width=(r-l)+'%'; band.style.background=color;
  mark.style.left=m+'%'; mark.style.background=color;
}

/* ---- render dashboard ---- */
function comfortCfg(d){
  var c=d.comfort; if(!c){ c=(d.unit==='C')?{tmin:20,tmax:24,hmin:40,hmax:60}:{tmin:68,tmax:76,hmin:40,hmax:60}; }
  return c;
}
function renderDash(d){
  var unit=d.unit||'F', c=comfortCfg(d);
  var tAbs=(unit==='C')?[16,29]:[60,85], hAbs=[20,80];
  $('tUnit').textContent='°'+unit;
  // comfort verdict (needs temp+rh)
  var vw=$('vWord'); var glow=$('glow'), hero=document.querySelector('.hero');
  if(d.hdc_ok && d.temp!=null && d.rh!=null){
    var cf=comfort(d.temp,d.rh,c);
    vw.textContent=cf.word; vw.style.color=cf.color; $('vNote').textContent=cf.note;
    glow.style.setProperty('--cc',cf.color);
    hero.style.setProperty('--cc',cf.color);
    hero.style.setProperty('--cc-wash','color-mix(in oklch,'+cf.color+' 10%,transparent)');
  } else { vw.textContent='No reading'; vw.style.color='var(--dim)'; $('vNote').textContent='Temperature/humidity sensor is offline.'; }
  // temp + humidity
  $('tVal').textContent=fmt(d.temp,1); $('hVal').textContent=fmt(d.rh,0);
  $('tIdeal').textContent='ideal '+c.tmin+'–'+c.tmax+'°';
  $('hIdeal').textContent='ideal '+c.hmin+'–'+c.hmax+'%';
  if(d.temp!=null) rangeBar('tBar',tAbs,[c.tmin,c.tmax],d.temp,SIG.t);
  if(d.rh!=null) rangeBar('hBar',hAbs,[c.hmin,c.hmax],d.rh,SIG.h);
  // pressure
  $('pVal').textContent=fmt(d.pressure,1);
  // iaq
  $('iVal').textContent=fmt(d.iaq,0);
  var is=iaqStat(d.iaq||0); $('iPill').innerHTML='<span class="d" style="background:'+is[1]+';box-shadow:0 0 9px '+is[1]+'"></span>'+is[0]; $('iPill').style.color=is[1];
  var info=iaqInfo(d.iaq_acc==null?1:d.iaq_acc, d.iaq); $('iNote').textContent=info.body;
  var cal=$('iCal');
  if(info.done){ cal.className='cal done'; cal.innerHTML='<span style="color:'+ST.good+';font-weight:600;font-size:12px">✓ Calibrated</span>'; }
  else { cal.className='cal'; var segs=''; for(var k=0;k<3;k++){segs+='<i style="background:'+(k<(d.iaq_acc||0)?ST.mod:'rgba(255,255,255,.13)')+'"></i>';}
    cal.innerHTML='<span class="segs">'+segs+'</span><span>Calibrating · <b style="color:var(--ink)">'+info.label+'</b> ('+(d.iaq_acc||0)+'/3)</span>'; }
}
function applyDeltas(){
  if(!HIST)return;
  function first(a){for(var i=0;i<a.length;i++)if(a[i]!=null&&!isNaN(a[i]))return a[i];return null;}
  function last(a){for(var i=a.length-1;i>=0;i--)if(a[i]!=null&&!isNaN(a[i]))return a[i];return null;}
  function dlt(a,dec,suf){var f=first(a),l=last(a);if(f==null||l==null)return '';var x=l-f;return (x>=0?'+':'−')+Math.abs(x).toFixed(dec)+(suf||'');}
  $('tDelta').textContent=dlt(HIST.t,1,'°')+' · 24h';
  $('hDelta').textContent=dlt(HIST.rh,0,'%')+' · 24h';
}

/* ---- diagnostics ---- */
function rows(el,list){ el.innerHTML=list.map(function(r){return '<div class="drow"><span class="k">'+r[0]+'</span><span class="v">'+r[1]+'</span></div>';}).join(''); }
function okPill(ok){var col=ok?ST.good:ST.poor;return '<span class="pill" style="color:'+col+'"><span class="d" style="background:'+col+';box-shadow:0 0 9px '+col+'"></span>'+(ok?'OK':'FAULT')+'</span>';}
function signal(rssi){var n=rssi>-55?4:rssi>-65?3:rssi>-75?2:1,b='';for(var i=0;i<4;i++){b+='<i style="height:'+(5+i*3)+'px;background:'+(i<n?ST.good:'rgba(255,255,255,.18)')+'"></i>';}return '<span class="bars">'+b+'</span>'+rssi+' dBm';}
function calCell(acc){if(acc>=3)return '<span style="color:'+ST.good+';font-weight:600">✓ Calibrated</span>';var lbl=['unreliable','low','medium','high'][acc]||'—';return 'Calibrating · '+lbl+' ('+acc+'/3)';}
function renderDiag(d){
  var on=d.hdc_ok, bon=d.bme_ok, unit=d.unit||'F', dash='—';
  rows($('dgSens'),[['HDC3022 (T/RH)',okPill(on)],['HDC reading age',on?ago(d.hdc_age):dash],
    ['BME688',okPill(bon)],['BME reading age',bon?ago(d.bme_age):dash],['BME calibration',calCell(d.iaq_acc==null?0:d.iaq_acc)]]);
  rows($('dgAq'),[['Raw BME temp',bon&&d.bme_temp!=null?fmt(d.bme_temp,1)+' °'+unit:dash],
    ['Raw BME humidity',bon&&d.bme_rh!=null?fmt(d.bme_rh,0)+' %':dash],
    ['eCO₂ (est.)',bon&&d.eco2!=null?fmt(d.eco2,0)+' ppm':dash],['bVOC',bon&&d.bvoc!=null?fmt(d.bvoc,2)+' ppm':dash]]);
  rows($('dgNet'),[['IP address',d.ip||dash],['Hostname',(d.hostname||'airbox')+'.local'],['WiFi SSID',d.ssid||dash],['WiFi RSSI',signal(d.rssi==null?-99:d.rssi)]]);
  rows($('dgSys'),[['Firmware','v'+(d.fw||'—')],['Uptime',ago(d.uptime)],['Free heap',d.heap==null?dash:(d.heap/1024).toFixed(0)+' KB'],['Last reset',d.reset_reason||dash]]);
}

/* ---- settings ---- */
var setUnit='F';
function fillTz(){var s=$('sTz');s.innerHTML=TZ.map(function(t,i){return '<option value="'+i+'">'+t+'</option>';}).join('');}
function loadSettings(d){
  $('sName').value=d.name||''; $('sUnit').value=d.unit||'F'; $('sHost').value=d.hostname||'';
  $('sTz').value=(d.tz!=null?d.tz:1); $('sBright').value=d.brightness!=null?d.brightness:64;
  $('sNMode').value=(d.night_mode!=null?d.night_mode:0); $('sNStart').value=(d.night_start!=null?d.night_start:23); $('sNEnd').value=(d.night_end!=null?d.night_end:7);
  setNight(!!d.night_en);
  var c=comfortCfg(d); setUnit=d.unit||'F';
  $('cuLbl').textContent='°'+setUnit; $('cuLbl2').textContent='°'+setUnit;
  $('sTmin').value=c.tmin; $('sTmax').value=c.tmax; $('sHmin').value=c.hmin; $('sHmax').value=c.hmax;
  $('aFw').textContent='v'+(d.fw||'—');
}
function setNight(on){ $('sNight').classList.toggle('on',on); $('nightSub').classList.toggle('off',!on); }
function save(){
  // comfort temp inputs are in setUnit; firmware stores °C and converts back, so send as-is with unit context
  var body='name='+encodeURIComponent($('sName').value)+'&unit='+$('sUnit').value+'&hostname='+encodeURIComponent($('sHost').value)
    +'&pass='+encodeURIComponent($('sPass').value)+'&brightness='+$('sBright').value
    +'&night_en='+($('sNight').classList.contains('on')?1:0)+'&night_mode='+$('sNMode').value
    +'&night_start='+$('sNStart').value+'&night_end='+$('sNEnd').value+'&tz='+$('sTz').value
    +'&comfort_tmin='+$('sTmin').value+'&comfort_tmax='+$('sTmax').value
    +'&comfort_hmin='+$('sHmin').value+'&comfort_hmax='+$('sHmax').value
    +'&comfort_unit='+setUnit;
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
    .then(function(){$('sMsg').textContent='✓ Saved — some changes apply after restart.';$('sPass').value='';setTimeout(poll,400);})
    .catch(function(){$('sMsg').textContent='✓ Saved (preview — no device).';$('sPass').value='';});
}
function post(path,confirmMsg){ if(confirmMsg&&!confirm(confirmMsg))return; fetch(path,{method:'POST'}).catch(function(){}); }

/* ---- poll ---- */
function poll(){
  getJSON('/api/data').catch(function(){return MOCK;}).then(function(d){
    DATA=d;
    $('conn').classList.toggle('bad',!(d.hdc_ok||d.bme_ok));
    $('stamp').textContent='updated '+new Date().toLocaleTimeString();
    $('hName').textContent=d.name||'AirBox'; document.title=d.name||'AirBox';
    $('hMeta').textContent=(d.hostname||'airbox')+'.local · v'+(d.fw||'—')+' · BME688 · HDC3022';
    renderDash(d); renderDiag(d);
    if(!settingsDirty) loadSettings(d);
  });
}
function loadHist(){ getJSON('/api/history').catch(function(){return MH;}).then(function(h){HIST=h;redraw();applyDeltas();}); }

/* ---- tabs ---- */
var settingsDirty=false;
function showTab(t){
  ['dash','diag','set'].forEach(function(s){$(s).classList.toggle('hide',s!==t);});
  document.querySelectorAll('.tabs button').forEach(function(b){b.classList.toggle('on',b.dataset.tab===t);});
  if(t==='dash') requestAnimationFrame(redraw);
}

/* ---- wire up ---- */
document.querySelectorAll('.tabs button').forEach(function(b){b.onclick=function(){showTab(b.dataset.tab);};});
fillTz();
$('sNight').onclick=function(){setNight(!$('sNight').classList.contains('on'));settingsDirty=true;};
['sName','sUnit','sHost','sTz','sBright','sNMode','sNStart','sNEnd','sPass','sTmin','sTmax','sHmin','sHmax'].forEach(function(id){
  var e=$(id); if(e) e.addEventListener('input',function(){settingsDirty=true;});
});
$('sUnit').addEventListener('change',function(){
  var nu=$('sUnit').value; if(nu===setUnit)return;
  var conv=(nu==='C')?function(v){return Math.round(f2c(+v));}:function(v){return Math.round(c2f(+v));};
  $('sTmin').value=conv($('sTmin').value); $('sTmax').value=conv($('sTmax').value);
  setUnit=nu; $('cuLbl').textContent='°'+nu; $('cuLbl2').textContent='°'+nu;
});
$('saveBtn').onclick=function(){settingsDirty=false;save();};
$('discardBtn').onclick=function(){settingsDirty=false;if(DATA)loadSettings(DATA);$('sPass').value='';$('sMsg').textContent='↩ Reverted to saved settings.';};
$('recalBtn').onclick=function(){post('/api/recalibrate','Recalibrate air sensor? IAQ accuracy resets and re-learns over 24–48 h.');};
$('recfgBtn').onclick=function(){post('/api/reconfigure','Reboot into WiFi setup portal now?');};
$('restartBtn').onclick=function(){post('/api/restart','Restart the device now?');};
window.addEventListener('resize',redraw);

poll(); loadHist();
setInterval(poll,4000); setInterval(loadHist,60000);
})();
</script>
</body>
</html>
)=====";

#endif // WEB_UI_H
