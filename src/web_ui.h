#pragma once
#include <Arduino.h>

#include "version.h"

// Control page served at "/". Polls /api/status every 2 s.
static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Paradox Bridge</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mute:#6b7280;--line:#e3e6ea;--ok:#16a34a;--warn:#d97706;--bad:#dc2626;--info:#2563eb}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f26;--fg:#e6e9ee;--mute:#9aa3af;--line:#2a313b}}
*{box-sizing:border-box}body{margin:0;font:14px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0}main{padding:16px;max-width:1100px;margin:auto;display:grid;gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
.card h2{font-size:14px;margin:0 0 10px;text-transform:uppercase;letter-spacing:.04em;color:var(--mute)}
.grid2{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;font-size:12px;font-weight:600;background:var(--line)}
.ok{background:#dcfce7;color:#166534}.warn{background:#fef3c7;color:#92400e}.bad{background:#fee2e2;color:#991b1b}.info{background:#dbeafe;color:#1e40af}
button{font:inherit;padding:6px 10px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button:hover{border-color:var(--info)}button.danger{border-color:var(--bad);color:var(--bad)}
.btns{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0}dt{color:var(--mute)}dd{margin:0}
.tablewrap{overflow-x:auto}table{border-collapse:collapse;width:100%}th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--mute);font-weight:600;font-size:12px}td.flags .pill{margin-right:4px}
#toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:8px;opacity:0;transition:.3s}
.fold>summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:8px}.fold>summary::-webkit-details-marker{display:none}.fold>summary::before{content:"\25B8";color:var(--mute);transition:transform .15s}.fold[open]>summary::before{transform:rotate(90deg)}.fold>summary h2{margin:0}.fold[open]>summary{margin-bottom:10px}.mute{color:var(--mute)}.ver{font-size:12px;font-weight:500;color:var(--mute)}
</style></head><body>
<header><h1>Paradox Bridge <span class="ver">v)HTML" FW_VERSION R"HTML(</span></h1><span id="link" class="pill">…</span><span id="alarm"></span>
<span class="mute" id="wifi"></span><span style="flex:1"></span>
<button onclick="post('/api/time')">Sync panel clock</button><button onclick="act('beep')">Beep</button><button id="panelBtn" onclick="panelToggle()">…</button><a href="/settings"><button>Settings</button></a><button class="danger" onclick="if(confirm('Reboot the bridge? The panel connection drops for about 15 seconds.'))post('/api/reboot')">Reboot</button></header>
<main>
<div class="grid2" id="parts"></div>
<div class="grid2">
 <div class="card"><h2>Panel</h2><dl id="panel"></dl></div>
 <div class="card"><h2>Troubles</h2><div id="troubles" class="mute">—</div></div>
</div>
<div class="card"><h2>Zones</h2><div class="tablewrap"><table><thead><tr><th>#</th><th>Name</th><th>Type / area</th><th>State</th><th>Flags</th><th>Signal</th><th></th></tr></thead><tbody id="zones"></tbody></table></div></div>
<details class="card fold" id="pgmCard"><summary><h2>PGM outputs</h2></summary><div class="tablewrap"><table><thead><tr><th>#</th><th>Label</th><th>Flags</th><th>Signal</th><th></th></tr></thead><tbody id="pgms"></tbody></table></div></details>
<div class="card"><h2>Events</h2><div class="tablewrap"><table><thead><tr><th>Time</th><th>Event</th><th>Group/Sub</th><th>Area</th></tr></thead><tbody id="events"></tbody></table></div></div>
</main><div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function el(tag,attrs,...kids){const e=document.createElement(tag);for(const k in attrs||{}){if(k==='onclick')e.onclick=attrs[k];else e.setAttribute(k,attrs[k])}for(const c of kids)e.append(c);return e}
const pill=(t,c)=>el('span',{class:'pill '+(c||'')},t);
function toast(t){const x=$('toast');x.textContent=t;x.style.opacity=1;clearTimeout(x._t);x._t=setTimeout(()=>x.style.opacity=0,2500)}
async function post(url,body){try{const r=await fetch(url,{method:'POST',body:new URLSearchParams(body||{})});const j=await r.json();toast(j.message)}catch(e){toast('request failed')}setTimeout(load,600)}
function act(action,extra,ask){if(ask&&!confirm(ask))return;post('/api/action',Object.assign({action},extra||{}))}
function flags(o,list){const td=el('td',{class:'flags'});for(const [k,t,c] of list)if(o[k])td.append(pill(t,c));return td}
const areas=m=>[1,2].filter(a=>m&(1<<(a-1))).map(a=>'A'+a).join('+');
let panelStopped=false;
function panelToggle(){
 if(panelStopped)post('/api/panel/connect');
 else if(confirm('Disconnect from the panel? Status and control stop until you press Connect.'))post('/api/panel/disconnect')}
function render(s){
 panelStopped=s.link.stopped;$('panelBtn').textContent=panelStopped?'Connect panel':'Disconnect panel';
 $('panelBtn').className=panelStopped?'':'danger';
 const ls=s.link.state;$('link').className='pill '+(ls==='online'?'ok':ls==='disconnected'?'bad':'warn');
 $('link').textContent='Panel '+ls+(s.link.error?' – '+s.link.error:'')+(s.link.configReading?' – reading configuration…':'');
 $('alarm').replaceChildren(s.panel.alarm?pill('SYSTEM IN ALARM','bad'):'');
 $('wifi').textContent=(s.wifi.connected?s.wifi.ssid+' '+s.wifi.ip+' ('+s.wifi.rssi+' dBm)':'')+(s.wifi.ap?'  AP '+s.wifi.apIp:'');
 $('parts').replaceChildren(...s.partitions.filter(p=>p.inUse).map(p=>{
  const st=p.alarm?pill('ALARM','bad'):p.armed?pill(p.mode.toUpperCase(),'warn'):pill('DISARMED','ok');
  const fl=[];for(const [k,t] of [['ready','ready'],['entryDelay','entry delay'],['exitDelay','exit delay'],['alarmMemory','alarm memory'],['zoneBypassed','bypass active'],['bell','bell'],['stayD','StayD'],['forceArmed','force armed']])if(p[k])fl.push(pill(t,'info'));
  const n={partition:p.id};
  return el('div',{class:'card'},el('h2',{},p.label+' (area '+p.id+')'),el('div',{},st,' ',...fl),
   el('div',{class:'btns'},
    el('button',{onclick:()=>act('arm_full',n,'Full arm '+p.label+'?')},'Full arm'),
    el('button',{onclick:()=>act('arm_stay',n,'Stay arm '+p.label+'?')},'Stay'),
    el('button',{onclick:()=>act('arm_sleep',n,'Sleep arm '+p.label+'?')},'Sleep'),
    el('button',{onclick:()=>act('arm_stay_d',n,'Stay arm with StayD?')},'Stay+StayD'),
    el('button',{class:'danger',onclick:()=>act('disarm',n,'Disarm '+p.label+'?')},'Disarm')))}));
 const pi=s.panel;$('panel').replaceChildren(...[
  ['Model',pi.detected?pi.model+' (0x'+pi.productId.toString(16)+')':'not detected'],['Firmware',pi.firmware],['Panel ID',pi.panelId],['Panel time',pi.time],
  ['AC power',pi.acFailure?'FAILURE':'OK'],['Battery',pi.batteryLow?'LOW / MISSING':'OK'],['AC / DC / battery (raw)',pi.acLevel+' / '+pi.dcLevel+' / '+pi.batteryLevel],['RF noise floor',pi.rfNoise],
  ['Frames rx / tx / bad (last '+Math.floor(s.link.countersAge/60)+' of 10 min)',s.link.framesRx+' / '+s.link.framesTx+' / '+s.link.checksumErrors],['Last action',s.link.lastAction||'—'],['MQTT',s.mqtt],['Firmware (bridge)','v'+s.version],
  ['Uptime',Math.floor(s.uptime/60)+' min']].flatMap(([k,v])=>[el('dt',{},k),el('dd',{},String(v))]));
 $('troubles').replaceChildren(...(s.troubles.length?s.troubles.map(t=>pill(t,'warn')):[document.createTextNode('No troubles')]));
 $('zones').replaceChildren(...s.zones.filter(z=>z.enabled).map(z=>el('tr',{},el('td',{},String(z.id)),el('td',{},z.label),el('td',{class:'mute'},z.definitionName+' · '+areas(z.partitions)),
  el('td',{},z.alarm?pill('ALARM','bad'):z.open?pill('open','warn'):pill('closed','ok')),
  flags(z,[['bypassed','bypassed','info'],['tamper','tamper','bad'],['wasAlarm','was in alarm','bad'],['fireTrouble','fire loop','bad'],['lowBattery','low battery','warn'],['supervision','supervision','warn'],['shutdown','shutdown','bad'],['exitDelay','exit delay','info']]),
  el('td',{},z.signal?z.signal+'/10':'—'),
  el('td',{},el('button',{onclick:()=>act('bypass',{zone:z.id},'Toggle bypass on '+z.label+'?')},'Bypass')))));
 $('pgms').replaceChildren(...s.pgms.map(g=>el('tr',{},el('td',{},String(g.id)),el('td',{},g.label),
  flags(g,[['tamper','tamper','bad'],['supervision','supervision','warn']]),el('td',{},g.signal?g.signal+'/10':'—'),
  el('td',{},el('button',{onclick:()=>act('pgm_on',{pgm:g.id})},'On'),' ',el('button',{onclick:()=>act('pgm_off',{pgm:g.id})},'Off')))));
 $('events').replaceChildren(...(s.events.length?s.events.map(e=>el('tr',{},el('td',{},e.time),el('td',{},e.text),el('td',{class:'mute'},e.group+'/'+e.sub),el('td',{},String(e.partition)))):[el('tr',{},el('td',{colspan:4,class:'mute'},'No events yet'))]));
}
async function load(){try{const r=await fetch('/api/status');if(r.ok)render(await r.json())}catch(e){$('link').textContent='bridge unreachable';$('link').className='pill bad'}}
// Remember whether the PGM card is open (per browser; collapsed by default).
(()=>{const d=$('pgmCard');try{d.open=localStorage.getItem('pgmOpen')==='1'}catch(e){}
 d.addEventListener('toggle',()=>{try{localStorage.setItem('pgmOpen',d.open?'1':'0')}catch(e){}})})();
load();setInterval(load,2000);
</script></body></html>)HTML";

// Settings page served at "/settings".
static const char kSettingsHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bridge Settings</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mute:#6b7280;--line:#e3e6ea;--bad:#dc2626;--info:#2563eb}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f26;--fg:#e6e9ee;--mute:#9aa3af;--line:#2a313b}}
*{box-sizing:border-box}body{margin:0;font:14px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;gap:16px;align-items:center;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0}header a{color:var(--info)}
main{padding:16px;max-width:720px;margin:auto;display:grid;gap:16px}
fieldset{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin:0}
legend{font-weight:600;padding:0 6px}
label{display:grid;grid-template-columns:minmax(120px,190px) 1fr;gap:4px 12px;align-items:center;margin:8px 0}
@media (max-width:520px){label{grid-template-columns:1fr}}
input,select{font:inherit;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);width:100%}
input[type=checkbox]{width:auto;justify-self:start}
.hint{grid-column:2;color:var(--mute);font-size:12px}@media (max-width:520px){.hint{grid-column:1}}
.row{display:flex;gap:8px}.row input{flex:1}
button{font:inherit;padding:7px 12px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--info);border-color:var(--info);color:#fff}button.danger{border-color:var(--bad);color:var(--bad)}
.actions{display:flex;flex-wrap:wrap;gap:8px}#msg{min-height:1.4em;font-weight:600}
#msg.err{color:var(--bad)}.ver{font-size:12px;font-weight:500;color:var(--mute)}
</style></head><body>
<header><h1>Bridge settings <span class="ver">v)HTML" FW_VERSION R"HTML(</span></h1><a href="/">&larr; Back to panel</a><span style="flex:1"></span><button type="button" class="danger" onclick="if(confirm('Reboot the bridge? Unsaved changes on this page are lost.'))post('/api/reboot')">Reboot</button></header>
<main><form id="f">
<fieldset><legend>WiFi network</legend>
 <label>Network name (SSID)<div class="row"><input name="wifiSsid" maxlength="32" list="ssids" placeholder="empty = access point only"><button type="button" id="scan">Scan</button></div></label>
 <datalist id="ssids"></datalist>
 <label>Password<input name="wifiPassword" type="password" maxlength="63" autocomplete="new-password"></label>
 <div class="hint" id="wifiPassHint"></div>
 <label>Open network (no password)<input name="wifiPasswordClear" type="checkbox"></label>
 <label>Hostname<input name="hostname" maxlength="32" pattern="[A-Za-z0-9-]+" required></label>
 <div class="hint">Reach the bridge at http://&lt;hostname&gt;.local</div>
</fieldset>
<fieldset><legend>Fallback access point</legend>
 <label>AP name<input name="apSsid" maxlength="32" required></label>
 <label>AP password<input name="apPassword" type="password" minlength="8" maxlength="63" autocomplete="new-password"></label>
 <div class="hint">Starts when there's no WiFi network or it can't be joined for 30 s. Leave the password empty to keep it.</div>
</fieldset>
<fieldset><legend>Web login</legend>
 <label>User<input name="webUser" maxlength="32" required></label>
 <label>New password<input name="webPassword" type="password" minlength="4" maxlength="64" autocomplete="new-password"></label>
 <div class="hint">Leave empty to keep the current password. Your browser will ask for the new login after the reboot.</div>
</fieldset>
<fieldset><legend>MQTT (Homebridge, Homey, ...)</legend>
 <label>Enable MQTT<input name="mqttEnabled" type="checkbox"></label>
 <div class="hint" id="mqttState"></div>
 <label>Broker address<input name="mqttHost" maxlength="64" placeholder="e.g. 192.168.1.10"></label>
 <label>Port<input name="mqttPort" type="number" min="1" max="65535"></label>
 <label>User<input name="mqttUser" maxlength="64" autocomplete="off"></label>
 <label>Password<input name="mqttPassword" type="password" maxlength="64" autocomplete="new-password"></label>
 <label>No password<input name="mqttPasswordClear" type="checkbox"></label>
 <label>Base topic<input name="mqttBase" maxlength="48" required pattern="[^+#]+"></label>
 <div class="hint">Prefix for every topic, e.g. <code>paradox</code> gives paradox/area/1/state, paradox/area/1/set, paradox/zone/3/open …</div>
 <label>Allow control over MQTT<input name="mqttAllowControl" type="checkbox"></label>
 <div class="hint">Arm / disarm / bypass / PGM commands. Untick for status only. Protect your broker with a password.</div>
 <label>Home Assistant discovery<input name="haDiscovery" type="checkbox"></label>
 <label>Discovery prefix<input name="haPrefix" maxlength="32" required pattern="[^+#]+"></label>
 <div class="actions" style="margin:6px 0"><button type="button" id="haPub">Publish Home Assistant discovery now</button></div>
 <div class="hint">Home Assistant creates a "Paradox alarm" device: an alarm panel per area and a motion / contact sensor per enabled zone (sensor type is set per zone below).</div>
</fieldset>
<fieldset><legend>Network firmware upload</legend>
 <label>Allow PlatformIO OTA<input name="otaNetwork" type="checkbox"></label>
 <div class="hint">ArduinoOTA on port 3232, password = web password. Uploads from the Firmware section below always work.</div>
</fieldset>
<fieldset><legend>Board</legend>
 <label>TTGO T-Display screen<input name="displayEnabled" type="checkbox"></label>
 <div class="hint">Untick for other ESP32 boards: the screen is never initialised and its pins (4, 5, 14, 16, 18, 19, 23, 34, 35) become free for the panel UART.</div>
</fieldset>
<fieldset><legend>Paradox panel</legend>
 <label>RX pin (from panel TX)<select name="rxPin"></select></label>
 <label>TX pin (to panel RX)<select name="txPin"></select></label>
 <div class="hint">Panel TX is 5 V: use a voltage divider on the RX pin.</div>
 <label>PC password [911]<input name="pcPassword" type="password" maxlength="4" pattern="[0-9A-Fa-f]{4}" autocomplete="off"></label>
 <div class="hint" id="pcHint">4 digits. Leave empty to keep the current value.</div>
 <label>Source ID<select name="sourceId">
  <option value="1">0x01 Winload direct</option><option value="2">0x02 Winload via IP module</option>
  <option value="3">0x03 Winload via GSM</option><option value="4">0x04 Winload via modem</option>
  <option value="5">0x05 NEware direct</option><option value="6">0x06 NEware via IP module</option>
  <option value="7">0x07 NEware via GSM</option><option value="8">0x08 NEware via modem</option>
  <option value="9">0x09 IP module (web page)</option><option value="10">0x0A VDMP3 direct</option>
  <option value="11">0x0B VDMP3 via GSM</option><option value="12">0x0C Remote</option></select></label>
 <label>User ID<input name="userId" placeholder="e.g. 0x0000"></label>
 <div class="hint">Decimal or hex (0x…), sent in bytes 34-35 of every request.</div>
 <label>Read configuration from panel<input name="readLabels" type="checkbox"></label>
</fieldset>
<fieldset><legend>Time</legend>
 <label>NTP server<input name="ntpServer" maxlength="64" required></label>
 <label>Time zone (POSIX TZ)<input name="tz" maxlength="64" required></label>
 <div class="hint">e.g. EET-2EEST,M3.5.0/3,M10.5.0/4 (Greece), CET-1CEST,M3.5.0,M10.5.0/3, GMT0BST,M3.5.0/1,M10.5.0</div>
</fieldset>
<div id="msg"></div>
<div class="actions"><button class="primary" type="submit">Save and reboot</button>
<button type="button" id="reboot">Reboot</button><button type="button" class="danger" id="reset">Reset to defaults</button></div>
</form>
<fieldset><legend>Zones</legend>
 <div class="hint" style="margin-bottom:8px">Read from the panel and stored on the bridge only - the panel's programming is never changed.
 <b>Auto</b> follows the panel (hidden if the panel has the zone disabled). Leave the name empty to use the panel's label.</div>
 <div id="cfgState" class="hint"></div>
 <div style="overflow-x:auto"><table style="border-collapse:collapse;width:100%"><thead><tr>
 <th>#</th><th>Panel label</th><th>Panel definition</th><th>Area</th><th>Name on the bridge</th><th>Sensor</th><th>Show</th><th></th></tr></thead>
 <tbody id="zt"></tbody></table></div>
 <div class="actions" style="margin-top:10px"><button type="button" id="reread">Re-read panel configuration</button>
 <button type="button" class="danger" id="zreset">Reset all zone names / modes</button></div>
</fieldset>
<fieldset><legend>Firmware update</legend>
 <div class="hint" style="margin-bottom:8px">Running v)HTML" FW_VERSION R"HTML(. Choose <code>firmware.bin</code> (from <code>.pio/build/ttgo-t-display/</code>) - the bridge installs it and reboots. Settings are kept.</div>
 <div class="actions"><input type="file" id="fw" accept=".bin" style="width:auto"><button type="button" class="primary" id="fwgo">Upload firmware</button></div>
 <progress id="fwp" max="100" value="0" style="width:100%;margin-top:10px" hidden></progress>
 <div id="fwmsg" class="hint"></div>
</fieldset>
</main>
<script>
const f=document.getElementById('f'),msg=document.getElementById('msg');
const show=(t,err)=>{msg.textContent=t;msg.className=err?'err':''};
function opts(sel,list,cur){sel.replaceChildren(...list.map(p=>{const o=document.createElement('option');o.value=p;o.textContent='GPIO'+p;if(p==cur)o.selected=true;return o}))}
async function load(){
 const c=await (await fetch('/api/config')).json();
 for(const k of ['wifiSsid','apSsid','hostname','webUser','ntpServer','tz'])f[k].value=c[k];
 f.userId.value='0x'+c.userId.toString(16).padStart(4,'0').toUpperCase();
 f.sourceId.value=c.sourceId;f.readLabels.checked=c.readLabels;
 f.displayEnabled.checked=c.displayEnabled;pinCfg=c.pins;
 f.mqttEnabled.checked=c.mqttEnabled;f.mqttHost.value=c.mqttHost;f.mqttPort.value=c.mqttPort;f.mqttUser.value=c.mqttUser;
 f.mqttBase.value=c.mqttBase;f.haDiscovery.checked=c.haDiscovery;f.haPrefix.value=c.haPrefix;f.mqttAllowControl.checked=c.mqttAllowControl;f.otaNetwork.checked=c.otaNetwork;
 f.mqttPassword.placeholder=c.mqttPasswordSet?'•••••• (unchanged)':'none';
 document.getElementById('mqttState').textContent='Status: '+c.mqttStatus;
 opts(f.rxPin,pinCfg[c.displayEnabled?'display':'generic'].rx,c.rxPin);opts(f.txPin,pinCfg[c.displayEnabled?'display':'generic'].tx,c.txPin);
 const keep='Leave empty to keep the current password';
 f.wifiPassword.placeholder=c.wifiPasswordSet?'•••••• (unchanged)':'none';
 f.apPassword.placeholder=c.apPasswordSet?'•••••• (unchanged)':'';
 f.webPassword.placeholder='•••••• (unchanged)';
 f.pcPassword.placeholder=c.pcPasswordSet?'•••• (unchanged)':'';
 document.getElementById('wifiPassHint').textContent=c.wifiPasswordSet?keep+'.':'';
}
async function post(url,body){
 try{const r=await fetch(url,{method:'POST',body});const j=await r.json();show(j.message,!j.ok);
  if(j.ok&&/Reboot/i.test(j.message))setTimeout(()=>location.reload(),12000)}catch(e){show('Request failed',true)}}
let pinCfg=null;
f.displayEnabled.onchange=()=>{if(!pinCfg)return;const k=f.displayEnabled.checked?'display':'generic';
 const rx=+f.rxPin.value,tx=+f.txPin.value;opts(f.rxPin,pinCfg[k].rx,rx);opts(f.txPin,pinCfg[k].tx,tx);
 if(!pinCfg[k].rx.includes(rx)||!pinCfg[k].tx.includes(tx))show('A selected pin is used by the display - check the RX/TX pins',true)};
f.onsubmit=e=>{e.preventDefault();post('/api/config',new URLSearchParams(new FormData(f)))};
document.getElementById('haPub').onclick=async()=>{
 try{const r=await fetch('/api/mqtt/republish',{method:'POST'});const j=await r.json();show(j.message,!j.ok)}catch(e){show('Request failed',true)}};
document.getElementById('reboot').onclick=()=>post('/api/reboot');
document.getElementById('reset').onclick=()=>{if(confirm('Erase all saved settings and reboot with the built-in defaults?'))post('/api/config/reset')};
document.getElementById('scan').onclick=async e=>{
 e.target.disabled=true;e.target.textContent='Scanning…';
 try{const l=await (await fetch('/api/scan')).json();
  document.getElementById('ssids').replaceChildren(...l.sort((a,b)=>b.rssi-a.rssi).map(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm'+(n.open?' open':'');return o}));
  show(l.length+' networks found - pick one from the name field');}catch(x){show('Scan failed',true)}
 e.target.disabled=false;e.target.textContent='Scan'};
load().catch(()=>show('Could not load settings',true));
const areaTxt=m=>[1,2].filter(a=>m&(1<<(a-1))).join(' + ');
function cell(t){const td=document.createElement('td');td.style.cssText='padding:5px 6px;border-bottom:1px solid var(--line);white-space:nowrap';if(t!==undefined)td.append(t);return td}
async function loadZones(){
 const s=await (await fetch('/api/status')).json();const L=s.link;
 document.getElementById('cfgState').textContent=L.configReading?'Reading configuration from the panel…':
  L.definitionsValid?('Panel configuration loaded'+(L.configCached?' and cached on the bridge':'')+'.'):'Panel configuration not read yet - all zones shown until it is.';
 document.getElementById('zt').replaceChildren(...s.zones.map(z=>{
  const name=document.createElement('input');name.value=z.customLabel;name.maxLength=32;name.placeholder=z.panelLabel;
  const mode=document.createElement('select');
  for(const [v,t] of [['auto','Auto ('+(z.panelEnabled?'shown':'hidden')+')'],['enabled','Always'],['disabled','Never']]){const o=document.createElement('option');o.value=v;o.textContent=t;if(v===z.mode)o.selected=true;mode.append(o)}
  const sensor=document.createElement('select');
  for(const [v,t] of [['contact','Contact (door/window)'],['motion','Motion']]){const o=document.createElement('option');o.value=v;o.textContent=t;if(v===z.sensor)o.selected=true;sensor.append(o)}
  const b=document.createElement('button');b.type='button';b.textContent='Save';
  b.onclick=()=>post('/api/zone',new URLSearchParams({zone:z.id,label:name.value,mode:mode.value,sensor:sensor.value})).then(loadZones);
  const tr=document.createElement('tr');if(!z.enabled)tr.style.opacity=.55;
  tr.append(cell(String(z.id)),cell(z.panelLabel),cell(z.definitionName+(z.definition!==255?' ('+z.definition+')':'')),
   cell(areaTxt(z.partitions)+' (raw '+z.partitionRaw+')'),cell(name),cell(sensor),cell(mode),cell(b));return tr}));
}
document.getElementById('reread').onclick=()=>post('/api/panel/reread').then(()=>setTimeout(loadZones,5000));
document.getElementById('zreset').onclick=()=>{if(confirm('Clear all zone names and modes on the bridge?'))post('/api/zones/reset').then(loadZones)};
loadZones();
document.getElementById('fwgo').onclick=()=>{
 const file=document.getElementById('fw').files[0],m=document.getElementById('fwmsg'),bar=document.getElementById('fwp');
 if(!file){m.textContent='Choose a firmware .bin file first';return}
 if(!confirm('Install '+file.name+' ('+Math.round(file.size/1024)+' KB) and reboot the bridge?'))return;
 const fd=new FormData();fd.append('firmware',file,file.name);
 const x=new XMLHttpRequest();x.open('POST','/api/update');bar.hidden=false;bar.value=0;m.textContent='Uploading…';
 x.upload.onprogress=e=>{if(e.lengthComputable){bar.value=e.loaded*100/e.total;m.textContent='Uploading… '+Math.round(bar.value)+'%'}};
 x.onload=()=>{let j={message:'HTTP '+x.status};try{j=JSON.parse(x.responseText)}catch(e){}
  m.textContent=j.message;if(j.ok)setTimeout(()=>location.reload(),15000)};
 x.onerror=()=>{m.textContent='Upload failed (connection lost)'};x.send(fd)};
</script></body></html>)HTML";
