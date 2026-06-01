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
  hdc_ok:true,bme_ok:true,rssi:-62,ssid:'my-wifi',ip:'192.168.1.42',uptime:14400,heap:102400,reset_reason:'Software',hdc_age:1,bme_age:1,
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
