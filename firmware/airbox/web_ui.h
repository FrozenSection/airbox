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
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AirBox</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#e6edf3;margin:0}
header{display:flex;align-items:center;justify-content:space-between;padding:14px 18px;background:#161b22;border-bottom:1px solid #21262d;position:sticky;top:0;z-index:5}
header h1{font-size:1.1rem;margin:0}
header .dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:#3fb950;margin-right:6px}
header .dot.bad{background:#f85149}
.tabs{display:flex;gap:4px;padding:10px 14px 0;max-width:1000px;margin:0 auto}
.tabs button{flex:1;padding:10px;border:0;border-radius:9px 9px 0 0;background:#161b22;color:#9fb0c3;font-size:.95rem;cursor:pointer}
.tabs button.on{background:#1f2630;color:#e6edf3;font-weight:600}
main{max-width:1000px;margin:0 auto;padding:14px 18px 40px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}
.card{background:#161b22;border:1px solid #21262d;border-radius:12px;padding:16px}
.card .lbl{font-size:.8rem;color:#9fb0c3;text-transform:uppercase;letter-spacing:.04em}
.card .val{font-size:2.1rem;font-weight:700;margin-top:4px}
.card .unit{font-size:1rem;color:#9fb0c3;font-weight:500}
.card canvas{width:100%;height:70px;margin-top:10px;display:block}
.acc{font-size:.78rem;margin-top:6px;color:#9fb0c3}
table{width:100%;border-collapse:collapse}
td{padding:8px 6px;border-bottom:1px solid #21262d;font-size:.92rem}
td:last-child{text-align:right;color:#c9d3df;font-variant-numeric:tabular-nums}
.hide{display:none}
label{display:block;font-size:.85rem;color:#9fb0c3;margin:12px 0 4px}
input,select{width:100%;padding:10px;border-radius:8px;border:1px solid #33405a;background:#0e1320;color:#e6edf3;font-size:1rem}
button.act{padding:11px 14px;border:0;border-radius:9px;color:#fff;font-size:.95rem;font-weight:600;cursor:pointer;margin:6px 6px 0 0}
.b-blue{background:#2f6df6}.b-grey{background:#33405a}.b-amber{background:#9a6700}.b-red{background:#a4282f}
small{color:#8493a8;display:block;margin-top:10px;line-height:1.4}
#stamp{font-size:.75rem;color:#6e7d90}
</style></head><body>
<header>
  <h1 id="name">AirBox</h1>
  <div><span class="dot" id="conn"></span><span id="stamp">—</span></div>
</header>
<div class="tabs">
  <button class="on" data-t="dash">Dashboard</button>
  <button data-t="diag">Diagnostics</button>
  <button data-t="set">Settings</button>
</div>
<main>
  <section id="dash" class="grid">
    <div class="card"><div class="lbl">Temperature</div><div class="val"><span id="temp">—</span><span class="unit" id="tu">°F</span></div><canvas id="cT"></canvas></div>
    <div class="card"><div class="lbl">Humidity</div><div class="val"><span id="rh">—</span><span class="unit">%</span></div><canvas id="cRH"></canvas></div>
    <div class="card"><div class="lbl">Pressure</div><div class="val"><span id="pres">—</span><span class="unit">hPa</span></div><canvas id="cP"></canvas></div>
    <div class="card"><div class="lbl">Air Quality (IAQ)</div><div class="val"><span id="iaq">—</span></div><div class="acc" id="iaqacc"></div><canvas id="cI"></canvas></div>
  </section>

  <section id="diag" class="hide"><div class="card"><table id="diagtbl"></table>
    <small>Raw BME688 T/RH read high due to gas-heater self-heating — trust the
    HDC3022 for ambient temperature/humidity. eCO₂ and bVOC are BSEC estimates
    derived from the same gas signal as IAQ. IAQ accuracy 3 = fully calibrated.</small>
  </div>
  <div class="card">
    <div class="lbl">Export data (CSV)</div>
    <p style="font-size:.88rem;color:#9fb0c3;margin:8px 0">Downloads the
    <b>last 7 days</b> of readings at 5-minute spacing (stored on the device,
    survives reboots). The dashboard chart above shows the most recent 24 h.</p>
    <button class="act b-blue" id="csvAll">Download CSV (7 days)</button>
    <small>Columns: timestamp (device local time), temperature, humidity, pressure, IAQ.</small>
  </div></section>

  <section id="set" class="hide"><div class="card">
    <label for="sName">Device name</label><input id="sName">
    <label for="sUnit">Temperature unit</label>
    <select id="sUnit"><option value="F">Fahrenheit (°F)</option><option value="C">Celsius (°C)</option></select>
    <label for="sHost">mDNS hostname (.local)</label><input id="sHost">
    <label for="sBright">Display brightness</label>
    <select id="sBright"><option value="8">Low (longest OLED life)</option><option value="64">Medium</option><option value="160">High</option><option value="255">Max</option></select>
    <div class="row" style="margin-top:12px"><input id="sNight" type="checkbox"><label for="sNight" style="margin:0">Night mode — blank or dim the screen on a schedule</label></div>
    <label for="sNMode">During night hours</label>
    <select id="sNMode"><option value="0">Turn screen off (blank)</option><option value="1">Dim screen</option></select>
    <div class="row" style="gap:10px">
      <div style="flex:1"><label for="sNStart">From (hour, 0–23)</label><input id="sNStart" type="number" min="0" max="23"></div>
      <div style="flex:1"><label for="sNEnd">Back to normal at (hour)</label><input id="sNEnd" type="number" min="0" max="23"></div>
    </div>
    <label for="sTz">Timezone (for timestamps &amp; night-mode clock; DST automatic)</label>
    <select id="sTz"><option value="0">UTC</option><option value="1">Eastern (New York)</option><option value="2">Central (Chicago)</option><option value="3">Mountain (Denver)</option><option value="4">Arizona (no DST)</option><option value="5">Pacific (Los Angeles)</option><option value="6">Alaska (Anchorage)</option><option value="7">Hawaii (no DST)</option><option value="8">UK (London)</option><option value="9">Central Europe</option><option value="10">India (Kolkata)</option><option value="11">Japan (Tokyo)</option><option value="12">Sydney</option></select>
    <label for="sPass">Admin password (blank = keep current)</label>
    <input id="sPass" type="password" placeholder="protects updates &amp; settings" autocomplete="off">
    <div id="mqttBox" class="hide">
      <label for="mHost">MQTT broker host</label><input id="mHost">
      <label for="mUser">MQTT user</label><input id="mUser">
      <label for="mPass">MQTT password (blank = keep)</label><input id="mPass" type="password" autocomplete="off">
    </div>
    <button class="act b-blue" id="save">Save settings</button>
    <button class="act b-grey" id="discard">Discard changes</button>
    <div id="smsg"></div>
    <hr style="border-color:#21262d;margin:18px 0">
    <button class="act b-grey" id="ota">Firmware update (OTA)</button>
    <button class="act b-amber" id="recal">Recalibrate air sensor</button>
    <button class="act b-grey" id="recfg">Reconfigure WiFi</button>
    <button class="act b-red" id="restart">Restart device</button>
    <small>Recalibrate clears the BSEC baseline — IAQ accuracy drops to 0 and
    re-learns over 24–48 h. Reconfigure WiFi reboots into the setup portal
    (you'll rejoin “AirBox-Setup” to pick a new network).</small>
  </div></section>
</main>
<script>
var unit='F',hist=null;
function $(i){return document.getElementById(i)}
// tabs
document.querySelectorAll('.tabs button').forEach(function(b){b.onclick=function(){
  document.querySelectorAll('.tabs button').forEach(function(x){x.classList.remove('on')});
  b.classList.add('on');['dash','diag','set'].forEach(function(s){$(s).classList.add('hide')});
  $(b.dataset.t).classList.remove('hide');
}});
function fmt(v,d){return (v==null||isNaN(v))?'—':Number(v).toFixed(d==null?1:d)}
function ago(s){if(s==null)return '—';if(s<90)return s+'s';if(s<5400)return Math.round(s/60)+'m';return Math.round(s/3600)+'h'}
var ACC=['unreliable','low','medium','high'];

function span2str(sp){if(sp<5400)return Math.round(sp/60)+'m';var hr=sp/3600;return (hr<10?hr.toFixed(1):Math.round(hr))+'h';}
function draw(id,arr,color,minSpan,iv){
  var c=$(id);if(!c)return;var dpr=window.devicePixelRatio||1;
  var w=c.clientWidth,h=c.clientHeight;c.width=w*dpr;c.height=h*dpr;
  var x=c.getContext('2d');x.scale(dpr,dpr);x.clearRect(0,0,w,h);
  var v=arr.filter(function(p){return p!=null&&!isNaN(p)});
  if(v.length<2){x.fillStyle='#6e7d90';x.font='12px sans-serif';x.fillText('collecting…',6,h/2);return;}
  var dmn=Math.min.apply(null,v),dmx=Math.max.apply(null,v);
  // Don't zoom the y-axis tighter than minSpan, so a near-flat reading looks
  // flat instead of a dramatic slope; then add headroom padding.
  var mid=(dmn+dmx)/2,span=Math.max(dmx-dmn,minSpan);
  var lo=mid-span/2-span*0.18,hi=mid+span/2+span*0.18,rng=(hi-lo)||1;
  // bot reserves a row for the time axis. Points still fill the full width; the
  // axis label shows the ACTUAL span, so it reads right even with little data.
  var pad=6,bot=11,gx=w-pad*2,gy=h-pad-bot,n=arr.length,lx=0,ly=0;
  x.strokeStyle=color;x.lineWidth=1.8;x.beginPath();var started=false;
  for(var i=0;i<n;i++){var p=arr[i];if(p==null||isNaN(p)){started=false;continue;}
    var px=pad+gx*(i/(n-1)),py=pad+gy*(1-(p-lo)/rng);
    if(!started){x.moveTo(px,py);started=true;}else x.lineTo(px,py);lx=px;ly=py;}
  x.stroke();
  x.fillStyle=color;x.beginPath();x.arc(lx,ly,2.4,0,6.2832);x.fill();  // latest point
  x.fillStyle='#6e7d90';x.font='10px sans-serif';                       // actual data range
  x.fillText(dmx.toFixed(1),2,9);x.fillText(dmn.toFixed(1),2,pad+gy);
  x.font='9px sans-serif';                                              // time axis
  x.fillText('-'+span2str((n-1)*(iv||300)),2,h-1);
  x.textAlign='right';x.fillText('now',w-2,h-1);x.textAlign='left';
}
function redrawCharts(){if(!hist)return;var iv=hist.interval_s||300;
  draw('cT',hist.t,'#f78166',2,iv);draw('cRH',hist.rh,'#58a6ff',6,iv);
  draw('cP',hist.p,'#a371f7',4,iv);draw('cI',hist.iaq,'#3fb950',25,iv);}

function poll(){fetch('/api/data').then(function(r){return r.json()}).then(function(d){
  $('conn').classList.toggle('bad',!(d.hdc_ok||d.bme_ok));
  $('stamp').textContent='updated '+new Date().toLocaleTimeString();
  $('name').textContent=d.name||'AirBox';document.title=d.name||'AirBox';
  unit=d.unit||'F';$('tu').textContent='°'+unit;
  $('temp').textContent=fmt(d.temp);$('rh').textContent=fmt(d.rh,0);
  $('pres').textContent=fmt(d.pressure);$('iaq').textContent=fmt(d.iaq,0);
  $('iaqacc').textContent='calibration: '+(ACC[d.iaq_acc]||'—');
  // diagnostics
  var rows=[['Raw BME temp',fmt(d.bme_temp)+' °'+unit],['Raw BME humidity',fmt(d.bme_rh,0)+' %'],
    ['eCO₂ (estimated)',fmt(d.eco2,0)+' ppm'],['bVOC',fmt(d.bvoc,2)+' ppm'],
    ['IAQ accuracy',(ACC[d.iaq_acc]||'—')+' ('+(d.iaq_acc==null?'—':d.iaq_acc)+')'],
    ['WiFi RSSI',fmt(d.rssi,0)+' dBm'],['Uptime',ago(d.uptime)],
    ['Free heap',(d.heap==null?'—':(d.heap/1024).toFixed(0)+' KB')],
    ['HDC sensor',d.hdc_ok?'OK':'fault'],['BME sensor',d.bme_ok?'OK':'fault'],
    ['HDC reading age',ago(d.hdc_age)],['BME reading age',ago(d.bme_age)],
    ['Last reset',d.reset_reason||'—'],['Firmware',d.fw||'—']];
  $('diagtbl').innerHTML=rows.map(function(r){return '<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>'}).join('');
  if(d.mqtt_enabled)$('mqttBox').classList.remove('hide');
}).catch(function(){$('conn').classList.add('bad')});}

function loadHist(){fetch('/api/history').then(function(r){return r.json()}).then(function(d){hist=d;redrawCharts();})}

// settings
function loadSettings(){fetch('/api/data').then(function(r){return r.json()}).then(function(d){
  $('sName').value=d.name||'';$('sUnit').value=d.unit||'F';$('sHost').value=d.hostname||'';
  if(d.brightness!=null)$('sBright').value=d.brightness;
  $('sNight').checked=!!d.night_en;
  $('sNMode').value=(d.night_mode!=null?d.night_mode:0);
  $('sNStart').value=(d.night_start!=null?d.night_start:23);
  $('sNEnd').value=(d.night_end!=null?d.night_end:7);
  $('sTz').value=(d.tz!=null?d.tz:0);
  if(d.mqtt_enabled){$('mqttBox').classList.remove('hide');$('mHost').value=d.mqtt_host||'';$('mUser').value=d.mqtt_user||'';}
});}
$('save').onclick=function(){
  var b='name='+encodeURIComponent($('sName').value)+'&unit='+$('sUnit').value+
        '&hostname='+encodeURIComponent($('sHost').value)+'&pass='+encodeURIComponent($('sPass').value)+
        '&brightness='+$('sBright').value+'&night_en='+($('sNight').checked?1:0)+
        '&night_mode='+$('sNMode').value+
        '&night_start='+$('sNStart').value+'&night_end='+$('sNEnd').value+'&tz='+$('sTz').value;
  if(!$('mqttBox').classList.contains('hide'))
    b+='&mqtt_host='+encodeURIComponent($('mHost').value)+'&mqtt_user='+encodeURIComponent($('mUser').value)+'&mqtt_pass='+encodeURIComponent($('mPass').value);
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
    .then(function(){$('smsg').textContent='✅ Saved. Some changes apply after restart.';$('sPass').value='';loadHist();});
};
// Back out without saving: re-pull the device's currently-saved values into the
// form, throwing away any unsaved edits. Nothing was written, so this fully
// restores how it was before you started "mucking with it".
$('discard').onclick=function(){loadSettings();$('sPass').value='';$('smsg').textContent='↩︎ Reverted to saved settings (nothing was changed).';};
function act(path,confirmMsg){return function(){if(confirmMsg&&!confirm(confirmMsg))return;
  fetch(path,{method:'POST'}).then(function(){});}}
$('ota').onclick=function(){location.href='/update'};
$('recal').onclick=act('/api/recalibrate','Recalibrate air sensor? IAQ accuracy resets and re-learns over 24–48 h.');
$('recfg').onclick=act('/api/reconfigure','Reboot into WiFi setup portal now?');
$('restart').onclick=act('/api/restart','Restart the device now?');
// CSV export. datetime-local values are local time; convert to UTC epoch seconds
// to match the stored timestamps.
$('csvAll').onclick=function(){location.href='/api/history.csv'};

loadSettings();poll();loadHist();
setInterval(poll,4000);setInterval(loadHist,60000);
window.addEventListener('resize',redrawCharts);
</script></body></html>)=====";

#endif // WEB_UI_H
