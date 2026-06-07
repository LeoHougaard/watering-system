#include "api_server.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "moisture_manager.h"
#include "ota_update.h"
#include "pump_controller.h"
#include "recommendation_engine.h"
#include "scheduler.h"
#include "user_observation_store.h"
#include "wifi_provisioning.h"

static const char *HTML =
"<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Watering System</title>"
"<style>:root{--bg:#f7faf9;--panel:#ffffff;--text:#111827;--muted:#6b7280;--line:#dbe5e3;--accent:#15b8b1;--green:#178a2f;--blue:#1870c9;--violet:#7c3fc7;--red:#d6382d}body.dark{--bg:#050b11;--panel:#0b141b;--text:#f4f7fb;--muted:#aab3c0;--line:#26323d}*{box-sizing:border-box}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:var(--bg);color:var(--text)}.wrap{max-width:1180px;margin:auto;padding:18px}header,.band,.card{background:var(--panel);border:1px solid var(--line);border-radius:8px}header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:22px 26px;margin-bottom:16px}.brand{display:flex;gap:18px;align-items:center}.leaf{font-size:46px;color:var(--green)}h1{font-size:2rem;margin:0}h2{font-size:1.05rem;margin:0 0 12px}.sub,.muted{color:var(--muted)}button,input,select,textarea{font:inherit}button{border:1px solid var(--accent);background:transparent;color:var(--accent);padding:12px;border-radius:7px;cursor:pointer;min-height:44px}button.fill{background:var(--accent);color:white}.status{color:var(--green);font-weight:700}.band{padding:22px;margin-bottom:16px}.manual{display:grid;grid-template-columns:repeat(5,1fr);gap:18px}.run{min-height:130px;border-color:var(--green);color:var(--green);font-weight:800}.run b{display:block;font-size:3rem}.stop{background:var(--red);border-color:var(--red);color:white;font-weight:800}.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:16px}.card{padding:18px}.metric{font-size:3.6rem;font-weight:900;color:var(--blue);line-height:1}.violet{color:var(--violet)}label{display:block;margin:10px 0 6px;color:var(--muted);font-weight:650}input,select,textarea{width:100%;border:1px solid var(--line);border-radius:7px;padding:10px;background:var(--panel);color:var(--text)}textarea{min-height:72px}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}nav{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:16px}canvas{width:100%;height:220px;border:1px solid var(--line);border-radius:8px}@media(max-width:850px){.manual,.grid{grid-template-columns:1fr 1fr}header{align-items:flex-start}.run b{font-size:2.4rem}}@media(max-width:560px){.wrap{padding:10px}.manual,.grid{grid-template-columns:1fr}header{padding:16px}h1{font-size:1.55rem}.leaf{font-size:36px}}</style></head>"
"<body><main class=wrap><header><div class=brand><div class=leaf>♧</div><div><h1>Watering System</h1><div class=sub>ESP32-S3 • Local Only</div></div></div><div><div class=status>● Online</div><button onclick='toggleTheme()'>Theme</button></div></header>"
"<main class=wrap><nav><button onclick=\"show('dash')\">Home</button><button onclick=\"show('manual')\">Pump</button><button onclick=\"show('sensor')\">Sensor</button><button onclick=\"show('planters')\">Plants</button><button onclick=\"show('schedule')\">Schedule</button><button onclick=\"show('wifi')\">Wi-Fi</button><button onclick=\"show('recs')\">Tips</button><button onclick=\"show('history')\">Log</button></nav>"
"<section id=dash></section><section id=manual hidden></section><section id=sensor hidden></section><section id=schedule hidden></section><section id=planters hidden></section><section id=wifi hidden></section><section id=recs hidden></section><section id=history hidden></section></main>"
"<script>"
"let status={},planters=[],settings={},history=[],obs=[],recs=[],moisture=[],latest={};const secs=[300,600,900,1200];if(!localStorage.pin)localStorage.pin=prompt('Local PIN','1234')||'';document.body.className=localStorage.theme||'';"
"async function api(p,o){let r=await fetch(p,{headers:{'Content-Type':'application/json','X-Local-PIN':localStorage.pin||''},...(o||{})});return r.headers.get('content-type')?.includes('json')?r.json():r.text()}"
"function show(id){for(const s of document.querySelectorAll('section'))s.hidden=s.id!==id;render()}"
"async function refresh(){[status,planters,settings,history,obs,recs,moisture,latest]=await Promise.all([api('/api/v1/status'),api('/api/v1/planters'),api('/api/v1/settings'),api('/api/v1/history'),api('/api/v1/observations'),api('/api/v1/recommendations'),api('/api/v1/moisture'),api('/api/v1/moisture/latest')]);render();drawGraph()}"
"function card(t,b,c=''){return `<div class='card ${c}'><h3>${t}</h3>${b}</div>`}"
"function render(){let mp=latest.percent==null?'--':latest.percent;dash.innerHTML=`<div class=band><h2>1. MANUAL WATERING</h2><div class=manual>${secs.map(s=>`<button class=run onclick='run(${s})'><b>${s<60?s:s/60}</b>${s<60?'SEC':'MIN'}</button>`).join('')}<button class=stop onclick='stop()'>STOP</button></div></div><div class=grid>${card('2. MOISTURE SENSOR',`<div class=metric>${mp}%</div><div class=muted>Soil Moisture</div>`)}${card('4. RESERVOIR STATUS',`<div class=metric>${status.reservoir_ok?'OK':'LOW'}</div><div class=muted>${settings.reservoir_sensor_bypass?'Bypass active':'Sensor guarded'}</div>`)}${card('3. GRAPH VIEWER',`<button onclick=\"show('history')\">View Graph</button>`,'violet')}</div><div class=band><h2>5. SCHEDULE</h2><b>Daily at ${settings.watering_windows?.[0]?.start||'06:00'} for ${Math.round((settings.schedule?.duration_sec||120)/60)} minutes</b><button onclick=\"show('schedule')\" style='float:right'>Edit Schedule</button></div>`;"
"manual.innerHTML=`<div class='band'><h2>Manual Control</h2><p>Countdown: ${status.current_run_remaining_sec||0}s</p><div class=manual>${secs.map(s=>`<button class=run onclick='run(${s})'><b>${s<60?s:s/60}</b>${s<60?'SEC':'MIN'}</button>`).join('')}<button class=stop onclick='stop()'>E-STOP</button></div></div>`;"
"sensor.innerHTML=`<div class=grid><div class=card><h2>Moisture sensor</h2><p class=metric>${mp}%</p><p class=muted>Raw: ${latest.raw||0} on GPIO ${settings.moisture_gpio}</p><label><input type=checkbox id=moistEnabled ${settings.moisture_sensor_enabled?'checked':''}> Enable hourly ADC logging</label><label>ADC GPIO<input id=moistGpio type=number value='${settings.moisture_gpio||1}'></label><label>Dry raw<input id=dryRaw type=number value='${settings.moisture_dry_raw||3000}'></label><label>Wet raw<input id=wetRaw type=number value='${settings.moisture_wet_raw||1200}'></label><button onclick='saveSensorSettings()'>Save sensor settings</button></div><div class=card><h2>Moisture graph</h2><canvas id=moistCanvas width=700 height=260></canvas></div><div class=card><h2>Manual note</h2><label>Planter<select id=moistPlanter>${planters.map(p=>`<option value='${p.id}'>${p.name}</option>`).join('')}</select></label><label>Moisture %<input id=moistValue type=number min=0 max=100></label><button onclick='saveMoisture()'>Save reading note</button></div></div>`;"
"schedule.innerHTML=`<div class=grid><div class=card><h2>Schedule</h2><label><input type=checkbox id=auto ${settings.auto_mode_enabled?'checked':''}> Auto mode</label><label>Start <input id=start value='${settings.watering_windows?.[0]?.start||'06:00'}'></label><label>Duration seconds <input id=dur type=number min=1 max=300 value='${settings.schedule?.duration_sec||120}'></label><label>Seasonal multiplier <input id=mul type=number step=.1 min=.1 max=2 value='${settings.seasonal_multiplier||1}'></label><button onclick='saveSchedule()'>Save</button></div><div class=card><h2>Reservoir safety</h2><label><input type=checkbox id=resEnabled ${settings.reservoir_sensor_enabled?'checked':''}> Enable reservoir sensor</label><label><input type=checkbox id=resBypass ${settings.reservoir_sensor_bypass?'checked':''}> Bypass reservoir sensor</label><button onclick='saveSafety()'>Save safety settings</button><div class=row><button onclick='rain(12)'>Rain delay 12h</button><button onclick='rain(24)'>24h</button><button onclick='rain(48)'>48h</button></div></div></div>`;"
"planters.innerHTML=`<div class=grid>${planters.map(p=>card(p.name,`<label>Name<input id=n${p.id} value='${p.name}'></label><label>Plant type<input id=t${p.id} value='${p.plant_type}'></label><label>Container<input id=c${p.id} value='${p.container_size}'></label><label>Sun<input id=s${p.id} value='${p.sun_exposure}'></label><label>Need<select id=w${p.id}><option>low</option><option ${p.water_need=='medium'?'selected':''}>medium</option><option ${p.water_need=='high'?'selected':''}>high</option></select></label><label>Dripper<select id=d${p.id}><option>unknown</option><option>low</option><option ${p.dripper_setting=='medium'?'selected':''}>medium</option><option ${p.dripper_setting=='high'?'selected':''}>high</option></select></label><label>Notes<textarea id=o${p.id}>${p.notes}</textarea></label><button onclick='savePlanter(${p.id})'>Save profile</button><hr><select id=ob${p.id}><option>healthy</option><option>too_dry</option><option>too_wet</option><option>wilting</option><option>yellowing</option><option>poor_growth</option><option>dripper_changed</option><option>unknown</option></select><select id=sev${p.id}><option>low</option><option>medium</option><option>high</option></select><button onclick='observe(${p.id})'>Record observation</button>`)).join('')}</div>`;"
"wifi.innerHTML=`<div class=card><h2>Wi-Fi</h2><label>Network name<input id=wifiSsid autocomplete=off></label><label>Password<input id=wifiPassword type=password></label><button onclick='saveWifi()'>Save and restart</button></div>`;"
"recs.innerHTML=`<div class=grid>${recs.map(r=>card(r.category,`<p>${r.message}</p><p>${r.why}</p><button onclick=\"resolveRec('${r.id}')\">Done</button> <button onclick=\"snoozeRec('${r.id}')\">Snooze</button>`)).join('')||card('Recommendations','None')}</div>`;"
"history.innerHTML=`<div class=grid>${card('Moisture over time','<canvas id=historyCanvas width=700 height=260></canvas>')}${card('Daily pump runtime',sumRuntime())}${card('Manual vs scheduled',ratio())}${card('Reservoir low events',history.filter(e=>e.stopped_reason=='low_reservoir').length)}${card('Observation timeline',obs.map(o=>`Planter ${o.planter_id}: ${o.condition} (${o.severity})`).join('<br>')||'None')}</div>`}"
"function sumRuntime(){let s=history.reduce((a,e)=>a+(e.duration_sec||0),0);return `${Math.round(s/60)} minutes logged`}"
"function ratio(){let m=history.filter(e=>e.type=='manual').length,sc=history.filter(e=>e.type=='scheduled').length;return `${m} manual / ${sc} scheduled`}"
"async function run(s){await api('/api/v1/manual-run',{method:'POST',body:JSON.stringify({duration_sec:s,reason:'manual_user_request'})});refresh()}async function stop(){await api('/api/v1/manual-stop',{method:'POST'});refresh()}"
"async function saveSchedule(){settings.auto_mode_enabled=auto.checked;settings.watering_windows=[{start:start.value,end:'09:00'}];settings.schedule.duration_sec=+dur.value;settings.seasonal_multiplier=+mul.value;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}"
"async function saveSafety(){settings.reservoir_sensor_enabled=resEnabled.checked;settings.reservoir_sensor_bypass=resBypass.checked;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}"
"async function saveSensorSettings(){settings.moisture_sensor_enabled=moistEnabled.checked;settings.moisture_gpio=+moistGpio.value;settings.moisture_dry_raw=+dryRaw.value;settings.moisture_wet_raw=+wetRaw.value;settings.moisture_sample_interval_sec=3600;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}"
"async function rain(h){await api('/api/v1/rain-delay',{method:'POST',body:JSON.stringify({hours:h})});refresh()}"
"async function savePlanter(id){let p={id,name:eval('n'+id).value,plant_type:eval('t'+id).value,container_size:eval('c'+id).value,sun_exposure:eval('s'+id).value,water_need:eval('w'+id).value,dripper_setting:eval('d'+id).value,enabled:true,notes:eval('o'+id).value};await api('/api/v1/planters/'+id,{method:'POST',body:JSON.stringify(p)});refresh()}"
"async function saveWifi(){await api('/api/v1/wifi',{method:'POST',body:JSON.stringify({ssid:wifiSsid.value,password:wifiPassword.value})});alert('Saved. Device is restarting. Connect to your home Wi-Fi and find the controller IP in your router/device list.')}"
"async function saveMoisture(){let note=`${moistValue.value}% manual moisture reading`;await api('/api/v1/observations',{method:'POST',body:JSON.stringify({planter_id:+moistPlanter.value,condition:'moisture_manual',severity:'low',note})});refresh();show('sensor')}"
"async function observe(id){await api('/api/v1/observations',{method:'POST',body:JSON.stringify({planter_id:id,condition:eval('ob'+id).value,severity:eval('sev'+id).value,note:''})});refresh()}"
"function drawOne(id){let c=document.getElementById(id);if(!c)return;let x=c.getContext('2d'),w=c.width,h=c.height;x.clearRect(0,0,w,h);x.strokeStyle='#15b8b1';x.lineWidth=3;let d=moisture.slice(-48);if(!d.length)return;x.beginPath();d.forEach((p,i)=>{let px=i*(w-24)/Math.max(1,d.length-1)+12,py=h-18-(p.percent||0)*(h-36)/100;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();x.fillStyle=getComputedStyle(document.body).getPropertyValue('--muted');x.fillText('0%',8,h-6);x.fillText('100%',8,14)}function drawGraph(){drawOne('moistCanvas');drawOne('historyCanvas')}function toggleTheme(){document.body.className=document.body.className?'':'dark';localStorage.theme=document.body.className}async function resolveRec(id){await api('/api/v1/recommendations/'+id+'/resolve',{method:'POST'});refresh()}async function snoozeRec(id){await api('/api/v1/recommendations/'+id+'/snooze',{method:'POST',body:JSON.stringify({hours:24})});refresh()}setInterval(refresh,3000);refresh();</script></main></body></html>";

static const char *HTML2 =
"<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Watering System</title>"
"<style>:root{--bg:#f6f8f8;--panel:#fff;--text:#111827;--muted:#64748b;--line:#d7e0df;--accent:#12b8b2;--green:#188038;--blue:#1d6fd1;--red:#c9352b}body.dark{--bg:#071014;--panel:#0d171d;--text:#f3f7f8;--muted:#9aa8b6;--line:#25333b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1160px;margin:auto;padding:20px}.top,.panel,.card{background:var(--panel);border:1px solid var(--line);border-radius:8px}.top{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:22px 24px;margin-bottom:14px}.brand{display:flex;align-items:center;gap:16px}.mark{width:48px;height:48px;border-radius:8px;background:var(--accent);color:#fff;display:grid;place-items:center;font-weight:800}h1{margin:0;font-size:30px;line-height:1.1}h2{margin:0 0 14px;font-size:15px;letter-spacing:.02em;text-transform:uppercase}.sub,.muted{color:var(--muted)}.right{text-align:right}.status{color:var(--green);font-weight:700;margin-bottom:8px}button{font:inherit;min-height:42px;border:1px solid var(--accent);background:transparent;color:var(--accent);border-radius:7px;padding:10px 14px;cursor:pointer}.panel{padding:20px;margin-bottom:14px}.manual{display:grid;grid-template-columns:repeat(5,1fr);gap:12px}.run{min-height:108px;border-color:var(--green);color:var(--green);font-weight:700}.run b{display:block;font-size:38px;line-height:1}.stop{background:var(--red);border-color:var(--red);color:#fff;font-weight:800}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:14px}.card{padding:20px;min-height:150px}.metric{font-size:52px;font-weight:800;color:var(--blue);line-height:1}.actions{display:flex;align-items:center;justify-content:space-between;gap:12px}.nav{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}label{display:block;margin:10px 0 6px;color:var(--muted);font-weight:600}input,select,textarea{width:100%;border:1px solid var(--line);border-radius:7px;padding:10px;background:var(--panel);color:var(--text);font:inherit}textarea{min-height:72px}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}canvas{width:100%;height:220px;border:1px solid var(--line);border-radius:8px}@media(max-width:850px){.manual,.grid{grid-template-columns:1fr 1fr}.top{align-items:flex-start}.right{text-align:left}}@media(max-width:560px){.wrap{padding:10px}.manual,.grid{grid-template-columns:1fr}.top{padding:16px}.mark{width:40px;height:40px}h1{font-size:24px}.metric{font-size:44px}}</style></head>"
"<body><main class=wrap><header class=top><div class=brand><div class=mark>WS</div><div><h1>Watering System</h1><div class=sub>ESP32-S3 - Local Only</div></div></div><div class=right><div class=status>Online</div><button onclick='toggleTheme()'>Theme</button></div></header>"
"<nav class=nav><button onclick=\"show('dash')\">Home</button><button onclick=\"show('manual')\">Pump</button><button onclick=\"show('sensor')\">Sensor</button><button onclick=\"show('schedule')\">Schedule</button><button onclick=\"show('history')\">History</button></nav>"
"<section id=dash></section><section id=manual hidden></section><section id=sensor hidden></section><section id=schedule hidden></section><section id=history hidden></section></main>"
"<script>let status={},settings={},history=[],moisture=[],latest={};const secs=[300,600,900,1200];if(!localStorage.pin)localStorage.pin=prompt('Local PIN','1234')||'';document.body.className=localStorage.theme||'';"
"async function api(p,o){let r=await fetch(p,{headers:{'Content-Type':'application/json','X-Local-PIN':localStorage.pin||''},...(o||{})});return r.headers.get('content-type')?.includes('json')?r.json():r.text()}function show(id){for(const s of document.querySelectorAll('section'))s.hidden=s.id!==id;render()}"
"async function refresh(){[status,settings,history,moisture,latest]=await Promise.all([api('/api/v1/status'),api('/api/v1/settings'),api('/api/v1/history'),api('/api/v1/moisture'),api('/api/v1/moisture/latest')]);render();drawGraph()}function card(t,b){return `<div class=card><h2>${t}</h2>${b}</div>`}"
"function render(){let mp=latest.percent==null?'--':latest.percent;dash.innerHTML=`<div class=panel><h2>Manual watering</h2><div class=manual>${secs.map(s=>`<button class=run onclick='run(${s})'><b>${s<60?s:s/60}</b>${s<60?'sec':'min'}</button>`).join('')}<button class=stop onclick='stop()'>Stop</button></div></div><div class=grid>${card('Moisture',`<div class=metric>${mp}%</div><div class=muted>Soil moisture</div>`)}${card('Reservoir',`<div class=metric>${status.reservoir_ok?'OK':'LOW'}</div><div class=muted>${settings.reservoir_sensor_bypass?'Bypass active':'Sensor enabled'}</div>`)}${card('History',`<button onclick=\"show('history')\">View graph</button>`)}</div><div class='panel actions'><div><h2>Schedule</h2><b>Daily at ${settings.watering_windows?.[0]?.start||'06:00'} for ${Math.round((settings.schedule?.duration_sec||120)/60)} minutes</b></div><button onclick=\"show('schedule')\">Edit schedule</button></div>`;"
"manual.innerHTML=`<div class=panel><h2>Manual control</h2><p>Countdown: ${status.current_run_remaining_sec||0}s</p><div class=manual>${secs.map(s=>`<button class=run onclick='run(${s})'><b>${s<60?s:s/60}</b>${s<60?'sec':'min'}</button>`).join('')}<button class=stop onclick='stop()'>Stop</button></div></div>`;"
"sensor.innerHTML=`<div class=grid><div class=card><h2>Moisture sensor</h2><div class=metric>${mp}%</div><p class=muted>Raw ${latest.raw||0} on GPIO ${settings.moisture_gpio}</p><label><input type=checkbox id=moistEnabled ${settings.moisture_sensor_enabled?'checked':''}> Enable hourly ADC logging</label><label>ADC GPIO<input id=moistGpio type=number value='${settings.moisture_gpio||1}'></label><label>Dry raw<input id=dryRaw type=number value='${settings.moisture_dry_raw||3000}'></label><label>Wet raw<input id=wetRaw type=number value='${settings.moisture_wet_raw||1200}'></label><button onclick='saveSensorSettings()'>Save sensor settings</button></div><div class=card><h2>Moisture graph</h2><canvas id=moistCanvas width=700 height=260></canvas></div></div>`;"
"schedule.innerHTML=`<div class=grid><div class=card><h2>Schedule</h2><label><input type=checkbox id=auto ${settings.auto_mode_enabled?'checked':''}> Auto mode</label><label>Start<input id=start value='${settings.watering_windows?.[0]?.start||'06:00'}'></label><label>Duration seconds<input id=dur type=number min=1 max=300 value='${settings.schedule?.duration_sec||120}'></label><button onclick='saveSchedule()'>Save schedule</button></div><div class=card><h2>Reservoir safety</h2><label><input type=checkbox id=resEnabled ${settings.reservoir_sensor_enabled?'checked':''}> Enable reservoir sensor</label><label><input type=checkbox id=resBypass ${settings.reservoir_sensor_bypass?'checked':''}> Bypass reservoir sensor</label><button onclick='saveSafety()'>Save safety settings</button></div></div>`;"
"history.innerHTML=`<div class=grid>${card('Moisture over time','<canvas id=historyCanvas width=700 height=260></canvas>')}${card('Pump runtime',`${Math.round(history.reduce((a,e)=>a+(e.duration_sec||0),0)/60)} minutes logged`)}${card('Runs',`${history.filter(e=>e.type=='manual').length} manual / ${history.filter(e=>e.type=='scheduled').length} scheduled`)}</div>`}async function run(s){await api('/api/v1/manual-run',{method:'POST',body:JSON.stringify({duration_sec:s,reason:'manual_user_request'})});refresh()}async function stop(){await api('/api/v1/manual-stop',{method:'POST'});refresh()}"
"async function saveSchedule(){settings.auto_mode_enabled=auto.checked;settings.watering_windows=[{start:start.value,end:'09:00'}];settings.schedule.duration_sec=+dur.value;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}async function saveSafety(){settings.reservoir_sensor_enabled=resEnabled.checked;settings.reservoir_sensor_bypass=resBypass.checked;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}async function saveSensorSettings(){settings.moisture_sensor_enabled=moistEnabled.checked;settings.moisture_gpio=+moistGpio.value;settings.moisture_dry_raw=+dryRaw.value;settings.moisture_wet_raw=+wetRaw.value;settings.moisture_sample_interval_sec=3600;await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});refresh()}"
"function drawOne(id){let c=document.getElementById(id);if(!c)return;let x=c.getContext('2d'),w=c.width,h=c.height,d=moisture.slice(-48);x.clearRect(0,0,w,h);x.strokeStyle='#12b8b2';x.lineWidth=3;if(!d.length)return;x.beginPath();d.forEach((p,i)=>{let px=i*(w-24)/Math.max(1,d.length-1)+12,py=h-18-(p.percent||0)*(h-36)/100;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}function drawGraph(){drawOne('moistCanvas');drawOne('historyCanvas')}function toggleTheme(){document.body.className=document.body.className?'':'dark';localStorage.theme=document.body.className}setInterval(refresh,3000);refresh();</script></body></html>";

static const char *HTML3 =
"<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Watering System</title>"
"<style>:root{--bg:#f6f8f8;--panel:#fff;--text:#111827;--muted:#64748b;--line:#d7e0df;--accent:#12b8b2;--green:#188038;--blue:#1d6fd1;--red:#c9352b}body.dark{--bg:#071014;--panel:#0d171d;--text:#f3f7f8;--muted:#9aa8b6;--line:#25333b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1180px;margin:auto;padding:20px}.top,.panel,.card,.notice{background:var(--panel);border:1px solid var(--line);border-radius:8px}.top{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:22px 24px;margin-bottom:14px}.brand{display:flex;align-items:center;gap:16px}.mark{width:48px;height:48px;border-radius:8px;background:var(--accent);color:#fff;display:grid;place-items:center;font-weight:800}h1{margin:0;font-size:30px;line-height:1.1}h2{margin:0 0 14px;font-size:15px;text-transform:uppercase;letter-spacing:.02em}.sub,.muted{color:var(--muted)}.right{text-align:right}.status{color:var(--green);font-weight:700;margin-bottom:8px}button{font:inherit;min-height:42px;border:1px solid var(--accent);background:transparent;color:var(--accent);border-radius:7px;padding:10px 14px;cursor:pointer}button[disabled]{opacity:.55;cursor:wait}.danger{background:var(--red);border-color:var(--red);color:#fff}.panel{padding:20px;margin-bottom:14px}.notice{display:none;padding:12px 14px;margin-bottom:14px}.notice.show{display:block}.notice.ok{border-color:var(--green)}.notice.err{border-color:var(--red)}.nav{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:14px}.card{padding:18px;min-height:138px}.metric{font-size:46px;font-weight:800;color:var(--blue);line-height:1}.manual{display:grid;grid-template-columns:repeat(5,1fr);gap:12px}.run{min-height:100px;border-color:var(--green);color:var(--green);font-weight:700}.run b{display:block;font-size:34px}.slot{display:grid;grid-template-columns:70px 120px 130px 1fr;gap:10px;align-items:end;border-top:1px solid var(--line);padding-top:12px;margin-top:12px}.days{display:grid;grid-template-columns:repeat(7,1fr);gap:4px}.days label{margin:0;text-align:center;font-size:12px}label{display:block;margin:10px 0 6px;color:var(--muted);font-weight:600}input,select,textarea{width:100%;border:1px solid var(--line);border-radius:7px;padding:10px;background:var(--panel);color:var(--text);font:inherit}input[type=checkbox]{width:auto}canvas{width:100%;height:220px;border:1px solid var(--line);border-radius:8px}.actions{display:flex;align-items:center;justify-content:space-between;gap:12px}@media(max-width:900px){.grid,.manual{grid-template-columns:1fr 1fr}.slot{grid-template-columns:1fr}.top{align-items:flex-start}.right{text-align:left}}@media(max-width:560px){.wrap{padding:10px}.grid,.manual{grid-template-columns:1fr}.top{padding:16px}h1{font-size:24px}.metric{font-size:40px}}</style></head>"
"<body><main class=wrap><header class=top><div class=brand><div class=mark>WS</div><div><h1>Watering System</h1><div class=sub>ESP32-S3 - Local Only</div></div></div><div class=right><div class=status>Online</div><button onclick='toggleTheme()'>Theme</button></div></header>"
"<nav class=nav><button onclick=\"show('dash')\">Status</button><button onclick=\"show('manual')\">Manual</button><button onclick=\"show('schedule')\">Schedule</button><button onclick=\"show('weather')\">Weather</button><button onclick=\"show('sensor')\">Sensors</button><button onclick=\"show('history')\">History</button><button onclick=\"show('wifi')\">Wi-Fi</button></nav><div id=notice class=notice></div>"
"<section id=dash></section><section id=manual hidden></section><section id=schedule hidden></section><section id=weather hidden></section><section id=sensor hidden></section><section id=history hidden></section><section id=wifi hidden></section></main>"
"<script>let status={},settings={},history=[],moisture=[],latest={},wifi={},active='dash',livePause=0,scheduleDirty=false,scheduleDraft=null;const secs=[300,600,900,1200],names=['sun','mon','tue','wed','thu','fri','sat'],labels=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];if(!localStorage.pin)localStorage.pin=prompt('Local PIN','1234')||'';document.body.className=localStorage.theme||'';"
"async function api(p,o){let r=await fetch(p,{headers:{'Content-Type':'application/json','X-Local-PIN':localStorage.pin||''},...(o||{})});let body=r.headers.get('content-type')?.includes('json')?await r.json():await r.text();if(!r.ok)throw new Error(body.error||body||('HTTP '+r.status));return body}function show(id){active=id;if(id=='schedule'&&!scheduleDirty)scheduleDraft=cloneSchedule();for(const s of document.querySelectorAll('section'))s.hidden=s.id!==id;render()}function note(t,k=''){if(k)livePause=Date.now()+5000;notice.className='notice show '+k;notice.textContent=t}function busy(on){document.querySelectorAll('button').forEach(b=>b.disabled=on)}function editingSchedule(){return active=='schedule'&&(scheduleDirty||(document.activeElement&&schedule.contains(document.activeElement)))}async function saveIt(label,fn){try{busy(true);note('Saving '+label+'...');fn();await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});note('Saved '+label+'.','ok');await refresh(true)}catch(e){note('Could not save '+label+': '+e.message,'err')}finally{busy(false)}}"
"async function refresh(force=false){try{[status,settings,history,moisture,latest,wifi]=await Promise.all([api('/api/v1/status'),api('/api/v1/settings'),api('/api/v1/history'),api('/api/v1/moisture'),api('/api/v1/moisture/latest'),api('/api/v1/wifi')]);ensureSlots();if(force||!editingSchedule()){if(!scheduleDirty)scheduleDraft=cloneSchedule();render();drawGraph()}liveNote()}catch(e){note('Controller status unavailable: '+e.message,'err')}}function ensureSlots(){if(!settings.schedule_slots)settings.schedule_slots=[{enabled:true,start:settings.watering_windows?.[0]?.start||'06:00',duration_sec:settings.schedule?.duration_sec||120,days:names.slice()}];settings.schedule_slots=settings.schedule_slots.slice().sort((a,b)=>(b.enabled?1:0)-(a.enabled?1:0));while(settings.schedule_slots.length<8)settings.schedule_slots.push({enabled:false,start:'06:00',duration_sec:120,days:[]})}"
"function card(t,b){return `<div class=card><h2>${t}</h2>${b}</div>`}function minText(s){return s<60?s+' sec':Math.round(s/60)+' min'}function prettyState(){return (status.pump_state||'UNKNOWN').replaceAll('_',' ').toLowerCase()}function weatherText(){if(!settings.weather_adjust_enabled)return 'Off';let m=settings.weather_last_multiplier||1;return `${Math.round(m*100)}% duration, ${settings.weather_last_temp_c||0} C, ${settings.weather_last_precip_mm||0} mm rain`}function liveNote(){if(Date.now()<livePause)return;let running=status.current_run_remaining_sec>0;let res=status.reservoir_bypass?'reservoir check bypassed':(status.reservoir_ok?'reservoir OK':'reservoir low');let msg=running?`Watering now, ${status.current_run_remaining_sec}s remaining.`:`Controller is ${prettyState()}.`;let cls=status.pump_state=='IDLE'&&status.reservoir_ok?'ok':'';note(`${msg} ${res}. ${status.block_reason||'Ready.'}`,cls)}"
"function render(){let mp=latest.percent==null?'--':latest.percent,can=status.pump_state=='IDLE'&&status.reservoir_ok;dash.innerHTML=`<div class=grid>${card('Pump status',`<div class=metric>${status.pump_state||'--'}</div><div class=muted>${status.current_run_remaining_sec||0}s remaining</div><div class=muted>IN1 GPIO${status.pump_gpio}: ${status.pump_gpio_level}, IN2 GPIO${status.pump_gpio_b}: ${status.pump_gpio_b_level}, SLEEP ${status.pump_sleep_gpio>=0?'GPIO'+status.pump_sleep_gpio+': '+status.pump_sleep_gpio_level:'not set'}</div><div class=muted>${status.block_reason||'Ready'}</div>`)}${card('Moisture',`<div class=metric>${mp}%</div><div class=muted>Soil sensor ${settings.moisture_sensor_enabled?'enabled':'disabled'} on GPIO${settings.moisture_gpio}</div>`)}${card('Next watering',`<div class=metric>${can?'Ready':'Hold'}</div><div class=muted>${status.next_scheduled||'No schedule'}</div><div class=muted>Weather: ${weatherText()}</div>`)}</div><div class='panel actions'><div><h2>Controller feedback</h2><b>${can?'Ready to water':'Watering blocked'}</b><div class=muted>${status.block_reason||'ESP32 state is live; this page refreshes every second.'}</div></div><button onclick=\"show('manual')\">Manual controls</button></div>`;"
"manual.innerHTML=`<div class=panel><h2>Manual watering</h2><p class=muted>${status.reservoir_ok?'Ready.':'Blocked: '+(status.block_reason||'reservoir sensor is low')}</p><div class=manual>${secs.map(s=>`<button class=run onclick='run(${s})'><b>${s<60?s:s/60}</b>${s<60?'sec':'min'}</button>`).join('')}<button class=danger onclick='stop()'>Stop</button></div></div>`;"
"let sd=scheduleDirty&&scheduleDraft?scheduleDraft:cloneSchedule();schedule.innerHTML=`<div class=grid><div class=card><h2>Schedule status</h2><div class=metric>${sd.auto?'On':'Off'}</div><div class=muted>${status.next_scheduled||'No enabled schedule'}</div><div class=muted>${scheduleSummary(sd.slots)}</div></div><div class=card><h2>Seasonal adjustment</h2><label>Seasonal multiplier<input id=season type=number step=.05 min=.1 max=2 value='${sd.seasonal}' oninput='captureSchedule()'></label><label>Max scheduled run<input id=maxSched type=number min=1 max=1200 value='${sd.maxRun}' oninput='captureSchedule()'></label></div></div><div class=panel><h2>Watering schedule</h2><label><input type=checkbox id=auto ${sd.auto?'checked':''} onchange='captureSchedule()'> Automatic watering enabled</label>${sd.slots.map((sl,i)=>slotHtml(sl,i)).join('')}<button onclick='saveSchedule()'>Save schedule</button></div>`;"
"weather.innerHTML=`<div class=grid><div class=card><h2>Weather adjustment</h2><p class=muted>Uses local weather for North Vancouver to adjust watering duration.</p><label><input type=checkbox id=wEn ${settings.weather_adjust_enabled?'checked':''}> Use weather adjustment</label><label><input type=checkbox id=wSkip ${settings.weather_skip_on_rain?'checked':''}> Skip only when current rain is heavy</label><label>Hot weather multiplier<input id=wHot type=number step=.05 min=1 max=1.25 value='${settings.weather_hot_multiplier||1.15}'></label><label>Rain multiplier<input id=wRain type=number step=.05 min=.65 max=1 value='${settings.weather_rain_multiplier||.75}'></label><button onclick='saveWeather()'>Save weather settings</button></div>${card('Current adjustment',`<div class=metric>${settings.weather_adjust_enabled?Math.round((settings.weather_last_multiplier||1)*100)+'%':'Off'}</div><div class=muted>${weatherText()}</div>`)}</div>`;"
"sensor.innerHTML=`<div class=grid><div class=card><h2>Moisture sensor</h2><div class=metric>${mp}%</div><p class=muted>Raw ${latest.raw||0}</p><label><input type=checkbox id=moistEnabled ${settings.moisture_sensor_enabled?'checked':''}> Enable hourly logging</label><label>ADC GPIO<input id=moistGpio type=number value='${settings.moisture_gpio||1}'></label><label>Dry raw<input id=dryRaw type=number value='${settings.moisture_dry_raw||3000}'></label><label>Wet raw<input id=wetRaw type=number value='${settings.moisture_wet_raw||1200}'></label><button onclick='saveSensorSettings()'>Save moisture sensor</button></div><div class=card><h2>Reservoir protection</h2><div class=metric>${status.reservoir_ok?'OK':'LOW'}</div><p class=muted>${status.reservoir_bypass?'Bypass is active. Watering is allowed even if the reservoir sensor reads low.':'Sensor is active. Watering stops if the reservoir reads low.'}</p><button onclick='toggleReservoirMode()'>${settings.reservoir_sensor_bypass?'Switch to reservoir sensor':'Switch to bypass mode'}</button></div><div class=card><h2>Pump relay</h2><p class=muted>Only change these if the relay wiring changed.</p><label>Relay IN1 GPIO<input id=pumpA type=number value='${settings.pump_gpio}'></label><label>Secondary off GPIO<input id=pumpB type=number value='${settings.pump_gpio_b}'></label><label>SLEEP/EN GPIO<input id=pumpSleep type=number value='${settings.pump_sleep_gpio}'></label><button onclick='savePumpDriver()'>Save pump relay</button></div><div class=card><h2>Moisture history</h2><canvas id=moistCanvas width=700 height=260></canvas></div></div>`;"
"history.innerHTML=`<div class=grid>${card('Moisture over time','<canvas id=historyCanvas width=700 height=260></canvas>')}${card('Pump runtime',`${Math.round(history.reduce((a,e)=>a+(e.duration_sec||0),0)/60)} minutes logged`)}${card('Runs',`${history.filter(e=>e.type=='manual').length} manual / ${history.filter(e=>e.type=='scheduled').length} scheduled`)}</div>`;wifi.innerHTML=`<div class=card><h2>Wi-Fi</h2><p class=muted>Current saved network: ${wifi.ssid||'not configured'}</p><label>Network name<input id=wifiSsid autocomplete=off></label><label>Password<input id=wifiPassword type=password></label><button onclick='saveWifi()'>Save and restart</button></div>`}"
"function daysText(ds){return !ds||!ds.length?'no days':ds.length==7?'every day':ds.map(d=>labels[names.indexOf(d)]).join(', ')}function slotText(sl){return `${sl.start||'--:--'} for ${minText(sl.duration_sec||0)} on ${daysText(sl.days)}`}function scheduleSummary(slots=settings.schedule_slots){let on=slots.filter(s=>s.enabled&&s.days&&s.days.length&&s.duration_sec>0);return on.length?on.map(slotText).join(' | '):'No active watering times.'}function cloneSchedule(){return {auto:!!settings.auto_mode_enabled,seasonal:settings.seasonal_multiplier||1,maxRun:settings.scheduled_max_run_sec||300,slots:(settings.schedule_slots||[]).map(s=>({enabled:!!s.enabled,start:s.start||'06:00',duration_sec:s.duration_sec||120,days:[...(s.days||[])]}))}}function captureSchedule(){if(!schedule.querySelector('#auto'))return scheduleDraft;scheduleDraft={auto:auto.checked,seasonal:+season.value||1,maxRun:+maxSched.value||300,slots:settings.schedule_slots.map((sl,i)=>({enabled:document.getElementById('se'+i).checked,start:document.getElementById('st'+i).value||'06:00',duration_sec:Math.max(1,+document.getElementById('sd'+i).value||1)*60,days:names.filter((n,d)=>document.getElementById('s'+i+'d'+d).checked)}))};scheduleDirty=true;return scheduleDraft}function slotHtml(sl,i){return `<div class=slot><label><input type=checkbox id=se${i} ${sl.enabled?'checked':''} onchange='captureSchedule()'> Enabled</label><label>Start time<input id=st${i} type=time value='${sl.start||'06:00'}' oninput='captureSchedule()'></label><label>Minutes<input id=sd${i} type=number min=1 max=20 step=1 value='${Math.max(1,Math.round((sl.duration_sec||120)/60))}' oninput='captureSchedule()'></label><div class=days>${names.map((n,d)=>`<label><input type=checkbox id=s${i}d${d} ${(sl.days||[]).includes(n)?'checked':''} onchange='captureSchedule()'>${labels[d]}</label>`).join('')}</div></div>`}"
"async function run(s){try{busy(true);note('Starting pump...');await api('/api/v1/manual-run',{method:'POST',body:JSON.stringify({duration_sec:s,reason:'manual_user_request'})});note('Pump command confirmed.','ok');await refresh()}catch(e){note('Pump command failed: '+e.message,'err')}finally{busy(false)}}async function stop(){try{busy(true);note('Stopping pump...');await api('/api/v1/manual-stop',{method:'POST'});note('Stop confirmed.','ok');await refresh()}catch(e){note('Stop failed: '+e.message,'err')}finally{busy(false)}}async function saveSchedule(){try{let draft=captureSchedule();busy(true);let compact=draft.slots.filter(s=>s.enabled&&s.days.length&&s.duration_sec>0);while(compact.length<8)compact.push({enabled:false,start:'06:00',duration_sec:120,days:[]});settings.auto_mode_enabled=draft.auto;settings.seasonal_multiplier=draft.seasonal;settings.scheduled_max_run_sec=Math.max(draft.maxRun,...compact.map(s=>s.enabled?s.duration_sec:0));settings.schedule_slots=compact;settings.schedule.duration_sec=compact[0]?.duration_sec||settings.schedule.duration_sec;settings.schedule.days=compact[0]?.days||settings.schedule.days;settings.watering_windows=[{start:compact[0]?.start||'06:00',end:'09:00'}];note('Saving schedule...');let saved=await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});settings=saved;ensureSlots();scheduleDirty=false;scheduleDraft=cloneSchedule();render();note('ESP32 confirmed: '+scheduleSummary(),'ok');await refresh(true)}catch(e){note('Schedule save failed: '+e.message,'err')}finally{busy(false)}}"
"async function saveWeather(){await saveIt('weather settings',()=>{settings.weather_adjust_enabled=wEn.checked;settings.weather_skip_on_rain=wSkip.checked;settings.weather_hot_multiplier=+wHot.value;settings.weather_rain_multiplier=+wRain.value})}async function toggleReservoirMode(){await saveIt('reservoir protection',()=>{settings.reservoir_sensor_enabled=true;settings.reservoir_sensor_bypass=!settings.reservoir_sensor_bypass})}async function saveSensorSettings(){await saveIt('moisture sensor',()=>{settings.moisture_sensor_enabled=moistEnabled.checked;settings.moisture_gpio=+moistGpio.value;settings.moisture_dry_raw=+dryRaw.value;settings.moisture_wet_raw=+wetRaw.value;settings.moisture_sample_interval_sec=3600})}async function savePumpDriver(){await saveIt('pump driver',()=>{settings.pump_gpio=+pumpA.value;settings.pump_gpio_b=+pumpB.value;settings.pump_sleep_gpio=+pumpSleep.value})}async function saveWifi(){try{busy(true);note('Saving Wi-Fi and restarting...');await api('/api/v1/wifi',{method:'POST',body:JSON.stringify({ssid:wifiSsid.value,password:wifiPassword.value})});note('Wi-Fi saved. Device is restarting.','ok')}catch(e){note('Could not save Wi-Fi: '+e.message,'err')}finally{busy(false)}}"
"function drawOne(id){let c=document.getElementById(id);if(!c)return;let x=c.getContext('2d'),w=c.width,h=c.height,d=moisture.slice(-48);x.clearRect(0,0,w,h);x.strokeStyle='#12b8b2';x.lineWidth=3;if(!d.length)return;x.beginPath();d.forEach((p,i)=>{let px=i*(w-24)/Math.max(1,d.length-1)+12,py=h-18-(p.percent||0)*(h-36)/100;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}function drawGraph(){drawOne('moistCanvas');drawOne('historyCanvas')}function toggleTheme(){document.body.className=document.body.className?'':'dark';localStorage.theme=document.body.className}setInterval(refresh,1000);refresh();</script></body></html>";

static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text);
    free(text);
    cJSON_Delete(json);
    return ESP_OK;
}

static char *read_body(httpd_req_t *req)
{
    char *buf = calloc(1, req->content_len + 1);
    if (!buf) return NULL;
    int got = httpd_req_recv(req, buf, req->content_len);
    if (got <= 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static bool require_pin(httpd_req_t *req)
{
    char pin[32] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Local-PIN", pin, sizeof(pin)) != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"error\":\"local PIN required\"}");
        return false;
    }
    if (strcmp(pin, "1234") != 0 && strcmp(pin, "water1234") != 0) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"invalid local PIN\"}");
        return false;
    }
    return true;
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, HTML3);
}

static esp_err_t status_get(httpd_req_t *req)
{
    runtime_state_t runtime = pump_controller_get_runtime();
    cJSON *root = runtime_to_json(&runtime);
    cJSON_AddStringToObject(root, "next_scheduled", scheduler_next_run_text());
    return send_json(req, root);
}

static esp_err_t planters_get(httpd_req_t *req)
{
    planter_config_t planters[PLANTER_COUNT];
    config_store_get_planters(planters);
    if (strstr(req->uri, "/api/v1/planters/")) {
        int id = atoi(strrchr(req->uri, '/') + 1);
        if (id >= 1 && id <= PLANTER_COUNT) return send_json(req, planter_to_json(&planters[id - 1]));
    }
    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < PLANTER_COUNT; i++) cJSON_AddItemToArray(array, planter_to_json(&planters[i]));
    return send_json(req, array);
}

static esp_err_t settings_get(httpd_req_t *req)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    return send_json(req, settings_to_json(&settings));
}

static esp_err_t history_get(httpd_req_t *req)
{
    char *text = history_store_read_json_array("watering");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text ? text : "[]");
    free(text);
    return ESP_OK;
}

static esp_err_t moisture_get(httpd_req_t *req)
{
    char *text = history_store_read_json_array("moisture");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text ? text : "[]");
    free(text);
    return ESP_OK;
}

static esp_err_t moisture_latest_get(httpd_req_t *req)
{
    moisture_reading_t reading = moisture_manager_get_latest();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", reading.enabled);
    cJSON_AddNumberToObject(root, "gpio", reading.gpio);
    cJSON_AddNumberToObject(root, "raw", reading.raw);
    if (reading.percent >= 0) cJSON_AddNumberToObject(root, "percent", reading.percent);
    else cJSON_AddNullToObject(root, "percent");
    return send_json(req, root);
}

static esp_err_t observations_get(httpd_req_t *req)
{
    char *text = user_observation_store_read_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text ? text : "[]");
    free(text);
    return ESP_OK;
}

static esp_err_t recs_get(httpd_req_t *req)
{
    char *text = recommendation_engine_get_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text ? text : "[]");
    free(text);
    return ESP_OK;
}

static esp_err_t diagnostics_get(httpd_req_t *req)
{
    char *text = history_store_read_json_array("diagnostics");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text ? text : "[]");
    free(text);
    return ESP_OK;
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    wifi_provisioning_credentials_t credentials;
    wifi_provisioning_get_credentials(&credentials);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "configured", credentials.configured);
    cJSON_AddStringToObject(root, "ssid", credentials.configured ? credentials.ssid : "");
    return send_json(req, root);
}

static esp_err_t manual_run_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    char *body = read_body(req);
    cJSON *json = cJSON_Parse(body);
    free(body);
    uint32_t duration = 30;
    const cJSON *d = cJSON_GetObjectItem(json, "duration_sec");
    const cJSON *reason = cJSON_GetObjectItem(json, "reason");
    if (cJSON_IsNumber(d)) duration = (uint32_t)d->valuedouble;
    char block_reason[96] = {0};
    esp_err_t can_run = pump_controller_can_manual_run(duration, block_reason, sizeof(block_reason));
    if (can_run != ESP_OK) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "409 Conflict");
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", block_reason[0] ? block_reason : "Pump cannot start.");
        return send_json(req, err);
    }
    esp_err_t err = pump_controller_manual_run(duration, cJSON_IsString(reason) ? reason->valuestring : "manual_user_request");
    cJSON_Delete(json);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        cJSON *fail = cJSON_CreateObject();
        cJSON_AddStringToObject(fail, "error", "Pump command queue did not accept the request.");
        return send_json(req, fail);
    }
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static esp_err_t stop_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    pump_controller_stop(STOP_MANUAL);
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static esp_err_t settings_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    char *body = read_body(req);
    cJSON *json = cJSON_Parse(body);
    free(body);
    system_settings_t settings;
    config_store_get_settings(&settings);
    json_to_settings(json, &settings);
    config_store_save_settings(&settings);
    cJSON_Delete(json);
    return settings_get(req);
}

static esp_err_t observation_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    char *body = read_body(req);
    cJSON *json = cJSON_Parse(body);
    free(body);
    user_observation_t o = {.timestamp = time(NULL)};
    snprintf(o.severity, sizeof(o.severity), "medium");
    const cJSON *p = cJSON_GetObjectItem(json, "planter_id");
    const cJSON *condition = cJSON_GetObjectItem(json, "condition");
    const cJSON *severity = cJSON_GetObjectItem(json, "severity");
    const cJSON *note = cJSON_GetObjectItem(json, "note");
    o.planter_id = cJSON_IsNumber(p) ? p->valueint : 1;
    snprintf(o.condition, sizeof(o.condition), "%s", cJSON_IsString(condition) ? condition->valuestring : "unknown");
    snprintf(o.severity, sizeof(o.severity), "%s", cJSON_IsString(severity) ? severity->valuestring : "medium");
    snprintf(o.note, sizeof(o.note), "%s", cJSON_IsString(note) ? note->valuestring : "");
    user_observation_store_add(&o);
    cJSON_Delete(json);
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static esp_err_t rain_delay_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    char *body = read_body(req);
    cJSON *json = cJSON_Parse(body);
    free(body);
    const cJSON *hours = cJSON_GetObjectItem(json, "hours");
    system_settings_t settings;
    config_store_get_settings(&settings);
    settings.rain_delay_enabled = true;
    settings.rain_delay_until = time(NULL) + (cJSON_IsNumber(hours) ? hours->valueint : 24) * 3600;
    config_store_save_settings(&settings);
    cJSON_Delete(json);
    return settings_get(req);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    char *body = read_body(req);
    cJSON *json = cJSON_Parse(body);
    free(body);

    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *password = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0' || !cJSON_IsString(password) ||
        strlen(ssid->valuestring) > 31 || strlen(password->valuestring) > 63) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"ssid and password required; ssid max 31 chars, password max 63 chars\"}");
        return ESP_OK;
    }

    wifi_provisioning_credentials_t credentials = {0};
    snprintf(credentials.ssid, sizeof(credentials.ssid), "%s", ssid->valuestring);
    snprintf(credentials.password, sizeof(credentials.password), "%s", password->valuestring);
    credentials.configured = true;
    esp_err_t err = wifi_provisioning_save_credentials(&credentials);
    cJSON_Delete(json);

    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"failed to save wifi credentials\"}");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "wifi_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t ota_apply_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    pump_controller_stop(STOP_MANUAL);
    return ota_update_apply_http_request(req);
}

static esp_err_t wildcard_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    const char *uri = req->uri;
    if (strstr(uri, "/planters/")) {
        int id = atoi(strrchr(uri, '/') + 1);
        char *body = read_body(req);
        cJSON *json = cJSON_Parse(body);
        free(body);
        planter_config_t planters[PLANTER_COUNT];
        config_store_get_planters(planters);
        if (id >= 1 && id <= PLANTER_COUNT) {
            json_to_planter(json, &planters[id - 1]);
            config_store_save_planter(&planters[id - 1]);
        }
        cJSON_Delete(json);
        return planters_get(req);
    }
    if (strstr(uri, "/resolve")) {
        char id[40] = {0};
        sscanf(uri, "/api/v1/recommendations/%39[^/]/resolve", id);
        recommendation_engine_resolve(id);
        return recs_get(req);
    }
    if (strstr(uri, "/snooze")) {
        char id[40] = {0};
        sscanf(uri, "/api/v1/recommendations/%39[^/]/snooze", id);
        recommendation_engine_snooze(id, 24);
        return recs_get(req);
    }
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static void reg(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = {.uri = uri, .method = method, .handler = handler};
    httpd_register_uri_handler(server, &u);
}

esp_err_t api_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 32;
    config.recv_wait_timeout = 120;
    config.stack_size = 8192;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    reg(server, "/", HTTP_GET, index_get);
    reg(server, "/api/v1/status", HTTP_GET, status_get);
    reg(server, "/api/v1/planters", HTTP_GET, planters_get);
    reg(server, "/api/v1/planters/*", HTTP_GET, planters_get);
    reg(server, "/api/v1/settings", HTTP_GET, settings_get);
    reg(server, "/api/v1/schedule", HTTP_GET, settings_get);
    reg(server, "/api/v1/history", HTTP_GET, history_get);
    reg(server, "/api/v1/moisture", HTTP_GET, moisture_get);
    reg(server, "/api/v1/moisture/latest", HTTP_GET, moisture_latest_get);
    reg(server, "/api/v1/observations", HTTP_GET, observations_get);
    reg(server, "/api/v1/recommendations", HTTP_GET, recs_get);
    reg(server, "/api/v1/diagnostics", HTTP_GET, diagnostics_get);
    reg(server, "/api/v1/wifi", HTTP_GET, wifi_get);
    reg(server, "/api/v1/manual-run", HTTP_POST, manual_run_post);
    reg(server, "/api/v1/manual-stop", HTTP_POST, stop_post);
    reg(server, "/api/v1/settings", HTTP_POST, settings_post);
    reg(server, "/api/v1/schedule", HTTP_POST, settings_post);
    reg(server, "/api/v1/observations", HTTP_POST, observation_post);
    reg(server, "/api/v1/rain-delay", HTTP_POST, rain_delay_post);
    reg(server, "/api/v1/wifi", HTTP_POST, wifi_post);
    reg(server, "/api/v1/planters/*", HTTP_POST, wildcard_post);
    reg(server, "/api/v1/recommendations/*", HTTP_POST, wildcard_post);
    reg(server, "/api/v1/ota/apply", HTTP_POST, ota_apply_post);
    return ESP_OK;
}
