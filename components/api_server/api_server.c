#include "api_server.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
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
#include "weather_manager.h"
#include "wifi_provisioning.h"

static const char *HTML3 =
"<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Watering System</title>"
"<style>:root{--bg:#f6f8f8;--panel:#fff;--text:#111827;--muted:#64748b;--line:#d7e0df;--accent:#12b8b2;--green:#188038;--blue:#1d6fd1;--red:#c9352b}body.dark{--bg:#071014;--panel:#0d171d;--text:#f3f7f8;--muted:#9aa8b6;--line:#25333b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1180px;margin:auto;padding:20px}.top,.panel,.card,.notice{background:var(--panel);border:1px solid var(--line);border-radius:8px}.top{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:22px 24px;margin-bottom:14px}h1{margin:0;font-size:30px;line-height:1.1}h2{margin:0 0 14px;font-size:15px;text-transform:uppercase;letter-spacing:.02em}.muted{color:var(--muted)}.status{color:var(--green);font-weight:700}button{font:inherit;min-height:42px;border:1px solid var(--accent);background:transparent;color:var(--accent);border-radius:7px;padding:10px 14px;cursor:pointer}button[disabled]{opacity:.55;cursor:wait}.danger{background:var(--red);border-color:var(--red);color:#fff}.panel{padding:20px;margin-bottom:14px}.notice{display:none;padding:12px 14px;margin-bottom:14px}.notice.show{display:block}.notice.ok{border-color:var(--green)}.notice.err{border-color:var(--red)}.nav{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}.nav button.on{background:var(--accent);color:#fff}.gear{font-size:20px;line-height:0}.grid,.statusgrid{display:grid;gap:14px;margin-bottom:14px}.grid{grid-template-columns:repeat(3,1fr)}.statusgrid{grid-template-columns:repeat(4,1fr)}.card{padding:18px;min-height:138px}.metric{font-size:46px;font-weight:800;color:var(--blue);line-height:1}.manual{display:grid;grid-template-columns:repeat(5,1fr);gap:12px}.run{min-height:100px;border-color:var(--green);color:var(--green);font-weight:700}.run b{display:block;font-size:28px}.runbox{display:grid;gap:10px;min-height:100px}.runbox label{margin:0}.wide{grid-column:1/-1}.statusGraph canvas{height:360px}.slot{display:grid;grid-template-columns:70px 120px 130px 1fr 90px;gap:10px;align-items:end;border-top:1px solid var(--line);padding-top:12px;margin-top:12px}.days{display:grid;grid-template-columns:repeat(7,1fr);gap:4px}.days label{margin:0;text-align:center;font-size:12px}.addtime{margin-top:14px}label{display:block;margin:10px 0 6px;color:var(--muted);font-weight:600}input,select,textarea{width:100%;border:1px solid var(--line);border-radius:7px;padding:10px;background:var(--panel);color:var(--text);font:inherit}input[type=checkbox]{width:auto}canvas{width:100%;height:220px;border:1px solid var(--line);border-radius:8px}.term{background:#050b11;color:#d1fae5;border-color:#17212b}.term pre{font-family:Consolas,monospace;white-space:pre-wrap;overflow:auto;max-height:430px}.cmd{display:flex;gap:8px}.cmd input{font-family:Consolas,monospace}@media(max-width:900px){.grid,.statusgrid,.manual{grid-template-columns:1fr 1fr}.slot{grid-template-columns:1fr}}@media(max-width:560px){.wrap{padding:10px}.grid,.statusgrid,.manual{grid-template-columns:1fr}.top{padding:16px}h1{font-size:24px}.metric{font-size:40px}.statusGraph canvas{height:300px}}</style></head>"
"<body><main class=wrap><header class=top><h1>Watering System</h1><div class=status>Online</div></header>"
"<nav class=nav><button data-p=dash onclick=\"show('dash')\">Status</button><button data-p=manual onclick=\"show('manual')\">Manual</button><button data-p=schedule onclick=\"show('schedule')\">Schedule</button><button data-p=prefs aria-label=Settings title=Settings onclick=\"show('prefs')\"><span class=gear aria-hidden=true>&#9881;</span></button></nav><div id=notice class=notice></div>"
"<section id=dash></section><section id=manual hidden></section><section id=schedule hidden></section><section id=prefs hidden></section></main>"
"<script>let status={},settings={},moisture=[],latest={},diag=[],active='dash',livePause=0,scheduleDirty=false,scheduleDraft=null,refreshBusy=false,themeChoice=localStorage.themeMode||(localStorage.theme?'dark':'auto');const runMinutes=[5,10,15,20],names=['sun','mon','tue','wed','thu','fri','sat'],labels=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'],themeMedia=matchMedia('(prefers-color-scheme: dark)');if(!localStorage.pin)localStorage.pin='1234';applyTheme();if(themeMedia.addEventListener)themeMedia.addEventListener('change',()=>{if(themeChoice=='auto')applyTheme()});"
"async function api(p,o){let r=await fetch(p,{headers:{'Content-Type':'application/json','X-Local-PIN':localStorage.pin||''},...(o||{})});let body=r.headers.get('content-type')?.includes('json')?await r.json():await r.text();if(!r.ok)throw new Error(body.error||body||('HTTP '+r.status));return body}async function syncTime(){if(status.time_valid)return;await api('/api/v1/time',{method:'POST',body:JSON.stringify({epoch_sec:Math.floor(Date.now()/1000)})})}function show(id){active=id;if(id=='schedule'&&!scheduleDirty)scheduleDraft=cloneSchedule();for(const s of document.querySelectorAll('section'))s.hidden=s.id!==id;document.querySelectorAll('.nav button').forEach(b=>b.classList.toggle('on',b.dataset.p==id));render()}function note(t,k=''){if(k)livePause=Date.now()+5000;notice.className='notice show '+k;notice.textContent=t}function busy(on){document.querySelectorAll('button').forEach(b=>b.disabled=on)}function clearDirty(id=active){let s=document.getElementById(id);if(s)delete s.dataset.dirty}function dirtySection(e){let s=e.target.closest('section');if(s&&s.id!='schedule'&&!e.target.closest('.term'))s.dataset.dirty=1}document.addEventListener('input',dirtySection);document.addEventListener('change',dirtySection);function editingSchedule(){return active=='schedule'&&(scheduleDirty||(document.activeElement&&schedule.contains(document.activeElement)))}function editingForm(){let s=document.getElementById(active),e=document.activeElement;return s&&active!='schedule'&&(s.dataset.dirty||(e&&e.id!='cmd'&&s.contains(e)&&/INPUT|SELECT|TEXTAREA/.test(e.tagName)))}function cancelEdit(){clearDirty();render();drawGraph()}async function saveIt(label,fn){try{busy(true);note('Saving '+label+'...');await syncTime();fn();settings=await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});clearDirty();note('Saved '+label+'.','ok');await refresh(true)}catch(e){note('Could not save '+label+': '+e.message,'err')}finally{busy(false)}}"
"async function refresh(force=false){if(refreshBusy)return;refreshBusy=true;try{let cv=document.getElementById('cmd')?.value||'';[status,settings,moisture,latest,diag]=await Promise.all([api('/api/v1/status'),api('/api/v1/settings'),api('/api/v1/moisture'),api('/api/v1/moisture/latest'),api('/api/v1/diagnostics')]);if(!status.time_valid){await syncTime();status=await api('/api/v1/status')}ensureSlots();if(force||(!editingSchedule()&&!editingForm())){if(!scheduleDirty)scheduleDraft=cloneSchedule();render();let ci=document.getElementById('cmd');if(ci){ci.value=cv;let o=document.getElementById('termOut');if(o)o.scrollTop=o.scrollHeight}drawGraph()}liveNote()}catch(e){note('Controller status unavailable: '+e.message,'err')}finally{refreshBusy=false}}async function refreshLive(){if(refreshBusy||document.hidden)return;refreshBusy=true;try{[status,latest]=await Promise.all([api('/api/v1/status'),api('/api/v1/moisture/latest')]);if(active=='dash'){renderDash(true);drawGraph()}else if((active=='manual'||active=='prefs')&&!editingForm()){render();drawGraph()}liveNote()}catch(e){note('Controller status unavailable: '+e.message,'err')}finally{refreshBusy=false}}function ensureSlots(){if(!settings.schedule_slots)settings.schedule_slots=[{enabled:true,start:settings.watering_windows?.[0]?.start||'06:00',duration_sec:settings.schedule?.duration_sec||120,days:names.slice()}];settings.schedule_slots=settings.schedule_slots.filter(s=>s.enabled&&s.days&&s.days.length&&s.duration_sec>0)}"
"function card(t,b){return `<div class=card><h2>${t}</h2>${b}</div>`}function durationText(s){s=Math.max(0,Math.round(s||0));let m=Math.floor(s/60),r=s%60;return r?`${m} min ${r} sec`:`${m} min`}function durationOptions(selected){return [1,2,5,10,30,60].map(v=>`<option value=${v} ${v==selected?'selected':''}>${durationText(v)}</option>`).join('')}function prettyState(){return (status.pump_state||'UNKNOWN').replaceAll('_',' ').toLowerCase()}function one(v){return Number(v||0).toFixed(1)}function weatherMetric(){if(!settings.weather_adjust_enabled)return 'Off';if(!settings.weather_data_fresh)return 'Normal';let m=settings.weather_last_multiplier??1;return m<.01?'Skip':Math.round(m*100)+'%'}function weatherText(){if(!settings.weather_adjust_enabled)return 'Weather adjustment is off';if(!settings.weather_data_fresh)return 'Forecast unavailable; using normal duration';let m=settings.weather_last_multiplier??1;return m<.01?'Watering skipped for rain':`${Math.round(m*100)}% duration, ${one(settings.weather_last_forecast_rain_mm)} mm expected`}function weatherReason(){if(!settings.weather_adjust_enabled)return 'Weather adjustment is off.';if(!settings.weather_data_fresh)return 'The saved forecast is missing or too old, so it cannot change watering.';let m=settings.weather_last_multiplier??1;if(m<.01)return 'Substantial recent rain or high-confidence rain soon means this run can be skipped.';if(m<.98)return 'Recent and expected rain reduce the amount of water needed.';if(m>1.02)return 'Higher evapotranspiration increases the amount of water needed.';return 'Current conditions are close to the normal watering demand.'}function weatherUpdated(){return settings.weather_last_update?new Date(settings.weather_last_update*1000).toLocaleString():'Not updated yet'}function liveNote(){if(Date.now()<livePause)return;let running=status.current_run_remaining_sec>0;let res=status.reservoir_bypass?'reservoir check bypassed':(status.reservoir_ok?'reservoir OK':'reservoir low');let msg=running?`Watering now, ${durationText(status.current_run_remaining_sec)} remaining.`:`Controller is ${prettyState()}.`;let cls=status.pump_state=='IDLE'&&status.reservoir_ok?'ok':'';note(`${msg} ${res}. ${status.block_reason||'Ready.'}`,cls)}"
"function dashCards(){let mp=latest.percent==null?'--':latest.percent,can=status.pump_state=='IDLE'&&status.reservoir_ok;return `${card('Pump status',`<div class=metric>${status.pump_state||'--'}</div><div class=muted>${durationText(status.current_run_remaining_sec)} remaining</div><div class=muted>${status.block_reason||'Ready'}</div>`)}${card('Moisture',`<div class=metric>${mp}%</div><div class=muted>Soil sensor ${settings.moisture_sensor_enabled?'enabled':'disabled'}</div>`)}${card('Next watering',`<div class=metric>${can?'Ready':'Hold'}</div><div class=muted>${status.next_scheduled||'No schedule'}</div>`)}${card('Weather',`<div class=metric>${weatherMetric()}</div><div class=muted>${weatherText()}</div>`)}`}function renderDash(keep=false){if(keep&&document.getElementById('dashGrid')){dashGrid.innerHTML=dashCards();return}dash.innerHTML=`<div id=dashGrid class=statusgrid>${dashCards()}</div><div class='panel statusGraph'><h2>Moisture history</h2><canvas id=dashMoistCanvas width=1000 height=360></canvas></div>`}"
"function render(){renderDash();let mp=latest.percent==null?'--':latest.percent,maxManualSec=Math.max(1,settings.manual_max_run_sec||1200),defaultManualMin=Math.min(5,maxManualSec/60);"
"manual.innerHTML=`<div class=panel><h2>Manual watering</h2><p class=muted>${status.reservoir_ok?'Ready.':'Blocked: '+(status.block_reason||'reservoir sensor is low')}</p><div class=manual>${runMinutes.map(m=>{let s=Math.min(m*60,maxManualSec);return `<button class=run onclick='run(${s})'><b>${durationText(s)}</b></button>`}).join('')}<div class='card runbox'><h2>Custom run</h2><label>Duration (minutes)<input id=manualMin type=number min=.1 max='${maxManualSec/60}' step=.1 value='${defaultManualMin}'></label><button class=run onclick='runManual()'><b>Run</b>Custom</button></div><button class=danger onclick='stop()'>Stop</button></div></div>`;"
"let sd=scheduleDirty&&scheduleDraft?scheduleDraft:cloneSchedule(),activeSlots=sd.slots.filter(s=>s.enabled&&s.days.length).length;schedule.innerHTML=`<div class=grid><div class=card><h2>Schedule status</h2><div class=metric>${activeSlots}</div><div class=muted>active watering ${activeSlots==1?'time':'times'}</div><div class=muted>${status.next_scheduled||'No enabled schedule'}</div><div class=muted>${scheduleSummary(sd.slots)}</div><button onclick='saveSchedule()'>Save schedule</button></div><div class=card><h2>Seasonal adjustment</h2><label>Seasonal multiplier<input id=season type=number step=.05 min=.1 max=2 value='${sd.seasonal}' oninput='captureSchedule()'></label><label>Maximum scheduled run (minutes)<input id=maxSchedMin type=number min=1 max=60 step=1 value='${sd.maxRunMin}' oninput='captureSchedule()'></label></div></div><div class=panel><h2>Watering schedule</h2>${sd.slots.map((sl,i)=>slotHtml(sl,i)).join('')||'<p class=muted>No watering times yet.</p>'}<button class=addtime onclick='addSlot()'>Add time</button></div>`;"
"prefs.innerHTML=`<div class=grid>${card('Appearance',`<p class=muted>Auto follows this browser or device without using ESP32 resources.</p><label>Theme<select id=themeMode onchange='saveTheme(this.value)'><option value=auto ${themeChoice=='auto'?'selected':''}>Auto</option><option value=light ${themeChoice=='light'?'selected':''}>Light</option><option value=dark ${themeChoice=='dark'?'selected':''}>Dark</option></select></label>`)}${card('Weather adjustment',`<p class=muted>Balances recent rain and plant water loss with the next 24-hour North Vancouver forecast.</p><label><input type=checkbox id=wEn ${settings.weather_adjust_enabled?'checked':''}> Automatically adjust watering</label><label><input type=checkbox id=wSkip ${settings.weather_skip_on_rain?'checked':''}> Allow a run to be skipped when substantial rain has fallen or is highly likely soon</label><button onclick='saveWeather()'>Save weather</button> <button onclick='refreshWeather()'>Refresh forecast</button>`)}${card('Weather details',`<div class=metric>${weatherMetric()}</div><div class=muted>${weatherReason()}</div><div class=muted>Updated ${weatherUpdated()}</div><p><b>${one(settings.weather_last_et0_mm)} mm</b> plant water demand</p><div class=muted>${one(settings.weather_last_recent_rain_mm)} mm recent rain; ${one(settings.weather_last_forecast_rain_mm)} mm expected</div><div class=muted>${Math.round(settings.weather_last_rain_probability_pct||0)}% weighted rain confidence</div>`)}${card('Moisture sensor',`<div class=metric>${mp}%</div><p class=muted>Live raw ${latest.raw||0}${latest.updated_at?' at '+new Date(latest.updated_at*1000).toLocaleTimeString():''}</p><label><input type=checkbox id=moistEnabled ${settings.moisture_sensor_enabled?'checked':''}> Enable moisture sensor</label><label>ADC GPIO<input id=moistGpio type=number value='${settings.moisture_gpio||1}'></label><label>Dry raw<input id=dryRaw type=number value='${settings.moisture_dry_raw||3000}'></label><label>Wet raw<input id=wetRaw type=number value='${settings.moisture_wet_raw||1200}'></label><label>Live read interval<select id=moistLiveInterval>${durationOptions(Math.max(1,Math.min(60,settings.moisture_live_interval_sec||5)))}</select></label><label>Graph sample interval (minutes)<input id=moistInterval type=number min=1 max=1440 value='${Math.max(1,Math.round((settings.moisture_sample_interval_sec||3600)/60))}'></label><label>History (days)<input id=moistDays type=number min=1 max=30 value='${settings.moisture_history_days||7}'></label><button onclick='saveSensorSettings()'>Save moisture sensor</button>`)}${card('Reservoir protection',`<div class=metric>${status.reservoir_ok?'OK':'LOW'}</div><p class=muted>${status.reservoir_bypass?'Bypass is active.':'Sensor is active.'}</p><button onclick='toggleReservoirMode()'>${settings.reservoir_sensor_bypass?'Switch to reservoir sensor':'Switch to bypass mode'}</button>`)}${card('Pump relay',`<label>Relay IN1 GPIO<input id=pumpA type=number value='${settings.pump_gpio}'></label><label>Secondary off GPIO<input id=pumpB type=number value='${settings.pump_gpio_b}'></label><label>SLEEP/EN GPIO<input id=pumpSleep type=number value='${settings.pump_sleep_gpio}'></label><button onclick='savePumpDriver()'>Save pump relay</button>`)}${card('Moisture history',`<canvas id=moistCanvas width=700 height=260></canvas>`)}</div><div class='panel term'><h2>Diagnostics</h2><pre id=termOut>${termText()}</pre><div class=cmd><input id=cmd aria-label='Diagnostic command' onkeydown=\"if(event.key=='Enter')cmdRun()\"><button onclick='cmdRun()'>Run</button></div></div>`}"
"function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}function fmt(t){return t>1700000000?new Date(t*1000).toLocaleString():'uptime '+durationText(t)}function termText(){let lines=diag.slice(-40).map(d=>`[${fmt(d.timestamp)}] ${d.message||JSON.stringify(d)}`);return esc((lines.join('\\n')||'No diagnostic events yet.')+'\\n> ')}function cmdRun(){let e=document.getElementById('cmd'),c=(e?.value||'').trim().toLowerCase();if(e)e.value='';if(c=='clear'){diag=[];render();return}let out={status,schedule:settings.schedule_slots,diag:diag.slice(-20)};diag.push({timestamp:Date.now()/1000,message:'console '+(c||'status')+': '+JSON.stringify(out[c]||out.status)});render()}function daysText(ds){return !ds||!ds.length?'no days':ds.length==7?'every day':ds.map(d=>labels[names.indexOf(d)]).join(', ')}function slotText(sl){return `${sl.start||'--:--'} for ${durationText(sl.duration_sec||0)} on ${daysText(sl.days)}`}function scheduleSummary(slots=settings.schedule_slots){let on=slots.filter(s=>s.enabled&&s.days&&s.days.length&&s.duration_sec>0);return on.length?on.map(slotText).join(' | '):'No active watering times.'}function cloneSchedule(){return {seasonal:settings.seasonal_multiplier||1,maxRunMin:(settings.scheduled_max_run_sec||300)/60,slots:(settings.schedule_slots||[]).map(s=>({enabled:!!s.enabled,start:s.start||'06:00',duration_sec:s.duration_sec||120,days:[...(s.days||[])]}))}}function captureSchedule(){if(!schedule.querySelector('#season'))return scheduleDraft;let base=scheduleDraft||cloneSchedule();scheduleDraft={seasonal:+season.value||1,maxRunMin:Math.max(1,+maxSchedMin.value||5),slots:base.slots.map((sl,i)=>({enabled:document.getElementById('se'+i).checked,start:document.getElementById('st'+i).value||'06:00',duration_sec:Math.max(1,+document.getElementById('sd'+i).value||1)*60,days:names.filter((n,d)=>document.getElementById('s'+i+'d'+d).checked)}))};scheduleDirty=true;return scheduleDraft}function slotHtml(sl,i){return `<div class=slot><label><input type=checkbox id=se${i} ${sl.enabled?'checked':''} onchange='captureSchedule()'> Enabled</label><label>Start<input id=st${i} type=time value='${sl.start||'06:00'}' oninput='captureSchedule()'></label><label>Duration (minutes)<input id=sd${i} type=number min=1 max=60 step=1 value='${Math.max(1,Math.round((sl.duration_sec||120)/60))}' oninput='captureSchedule()'></label><div class=days>${names.map((n,d)=>`<label><input type=checkbox id=s${i}d${d} ${(sl.days||[]).includes(n)?'checked':''} onchange='captureSchedule()'>${labels[d]}</label>`).join('')}</div><button class=danger onclick='removeSlot(${i})'>Remove</button></div>`}function addSlot(){let d=captureSchedule()||cloneSchedule();if(d.slots.length>=8)return note('Maximum 8 schedule times.','err');d.slots.push({enabled:true,start:'06:00',duration_sec:120,days:names.slice()});scheduleDraft=d;scheduleDirty=true;render()}function removeSlot(i){let d=captureSchedule()||cloneSchedule();d.slots.splice(i,1);scheduleDraft=d;scheduleDirty=true;render()}"
"function runManual(){let m=Math.max(1,settings.manual_max_run_sec||1200),v=Math.max(1,Math.min(m,Math.round((+manualMin.value||.1)*60)));manualMin.value=Math.round(v/6)/10;clearDirty('manual');run(v)}async function run(s){try{busy(true);clearDirty('manual');note('Starting pump for '+durationText(s)+'...');await api('/api/v1/manual-run',{method:'POST',body:JSON.stringify({duration_sec:s,reason:'manual_user_request'})});note('Pump command confirmed.','ok');await refresh()}catch(e){note('Pump command failed: '+e.message,'err')}finally{busy(false)}}async function stop(){try{busy(true);clearDirty('manual');note('Stopping pump...');await api('/api/v1/manual-stop',{method:'POST'});note('Stop confirmed.','ok');await refresh()}catch(e){note('Stop failed: '+e.message,'err')}finally{busy(false)}}async function saveSchedule(){try{let draft=captureSchedule();busy(true);await syncTime();let compact=draft.slots.filter(s=>s.enabled&&s.days.length&&s.duration_sec>0);while(compact.length<8)compact.push({enabled:false,start:'06:00',duration_sec:120,days:[]});settings.auto_mode_enabled=true;settings.seasonal_multiplier=draft.seasonal;settings.scheduled_max_run_sec=Math.max(Math.round(draft.maxRunMin*60),...compact.map(s=>s.enabled?s.duration_sec:0));settings.schedule_slots=compact;settings.schedule.duration_sec=compact[0]?.duration_sec||settings.schedule.duration_sec;settings.schedule.days=compact[0]?.days||settings.schedule.days;settings.watering_windows=[{start:compact[0]?.start||'06:00',end:'09:00'}];note('Saving schedule...');let saved=await api('/api/v1/settings',{method:'POST',body:JSON.stringify(settings)});settings=saved;ensureSlots();scheduleDirty=false;scheduleDraft=cloneSchedule();render();note('ESP32 confirmed: '+scheduleSummary(),'ok');await refresh(true)}catch(e){note('Schedule save failed: '+e.message,'err')}finally{busy(false)}}"
"async function saveWeather(){await saveIt('weather settings',()=>{settings.weather_adjust_enabled=wEn.checked;settings.weather_skip_on_rain=wSkip.checked})}async function refreshWeather(){try{busy(true);await syncTime();await api('/api/v1/weather/refresh',{method:'POST'});note('Forecast refresh requested. New data will appear shortly.','ok');setTimeout(()=>refresh(true),3000)}catch(e){note('Forecast refresh failed: '+e.message,'err')}finally{busy(false)}}async function toggleReservoirMode(){await saveIt('reservoir protection',()=>{settings.reservoir_sensor_enabled=true;settings.reservoir_sensor_bypass=!settings.reservoir_sensor_bypass})}async function saveSensorSettings(){await saveIt('moisture sensor',()=>{settings.moisture_sensor_enabled=moistEnabled.checked;settings.moisture_gpio=+moistGpio.value;settings.moisture_dry_raw=+dryRaw.value;settings.moisture_wet_raw=+wetRaw.value;settings.moisture_live_interval_sec=Math.max(1,Math.min(60,+moistLiveInterval.value||5));settings.moisture_sample_interval_sec=Math.max(60,(+moistInterval.value||60)*60);settings.moisture_history_days=Math.max(1,Math.min(30,+moistDays.value||7))})}async function savePumpDriver(){await saveIt('pump driver',()=>{settings.pump_gpio=+pumpA.value;settings.pump_gpio_b=+pumpB.value;settings.pump_sleep_gpio=+pumpSleep.value})}"
"function drawOne(id){let c=document.getElementById(id);if(!c)return;let x=c.getContext('2d'),w=c.width,h=c.height,days=settings.moisture_history_days||7,now=Math.floor(Date.now()/1000),start=now-days*86400,d=moisture.filter(p=>(p.timestamp||0)>=start&&p.percent!=null);let L=54,R=12,T=12,B=38,gw=w-L-R,gh=h-T-B;x.clearRect(0,0,w,h);x.font='12px system-ui';x.fillStyle=getComputedStyle(document.body).getPropertyValue('--muted')||'#64748b';x.strokeStyle=getComputedStyle(document.body).getPropertyValue('--line')||'#d7e0df';x.lineWidth=1;for(let y=0;y<=100;y+=25){let py=T+(100-y)*gh/100;x.beginPath();x.moveTo(L,py);x.lineTo(w-R,py);x.stroke();x.fillText(y+'%',8,py+4)}for(let i=0;i<=Math.min(days,7);i++){let t=start+i*days*86400/Math.min(days,7),px=L+(t-start)*gw/(days*86400),dt=new Date(t*1000);x.beginPath();x.moveTo(px,T);x.lineTo(px,h-B);x.stroke();x.fillText(dt.toLocaleDateString(undefined,{month:'short',day:'numeric'}),px-18,h-14)}x.strokeStyle='#12b8b2';x.lineWidth=3;if(!d.length){x.fillText('No moisture readings in selected range',L+10,T+28);return}x.beginPath();d.forEach((p,i)=>{let px=L+((p.timestamp||start)-start)*gw/(days*86400),py=T+(100-(p.percent||0))*gh/100;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}function drawGraph(){drawOne('dashMoistCanvas');drawOne('moistCanvas')}function applyTheme(v=themeChoice){themeChoice=['auto','light','dark'].includes(v)?v:'auto';document.body.classList.toggle('dark',themeChoice=='dark'||(themeChoice=='auto'&&themeMedia.matches))}function saveTheme(v){localStorage.themeMode=v;localStorage.removeItem('theme');applyTheme(v);clearDirty('prefs');render();note('Theme preference saved.','ok')}function liveLoop(){refreshLive().finally(()=>setTimeout(liveLoop,status.current_run_remaining_sec>0?1000:5000))}document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh(true)});setInterval(refresh,60000);refresh();liveLoop();</script></body></html>";

#define MAX_JSON_BODY_BYTES 8192

static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, text);
    free(text);
    cJSON_Delete(json);
    return ESP_OK;
}

static cJSON *read_json_body(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"JSON body required\"}");
        return NULL;
    }
    if (req->content_len > MAX_JSON_BODY_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "{\"error\":\"request body too large\"}");
        return NULL;
    }
    char *buf = calloc(1, req->content_len + 1);
    if (!buf) return NULL;
    size_t received = 0;
    int timeouts = 0;
    while (received < req->content_len) {
        int got = httpd_req_recv(req, buf + received, req->content_len - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT && timeouts++ == 0) continue;
        if (got <= 0) {
            free(buf);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"error\":\"incomplete request body\"}");
            return NULL;
        }
        received += (size_t)got;
    }
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"valid JSON object required\"}");
        return NULL;
    }
    return json;
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
    static bool seen = false;
    static pump_state_t last_state;
    static bool last_reservoir;
    static char last_block[64];
    scheduler_refresh_next_run();
    const char *next = scheduler_next_run_text();
    if (!seen || runtime.pump_state != last_state || runtime.reservoir_ok != last_reservoir ||
        strcmp(runtime.block_reason, last_block) != 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "status %s, reservoir %s, block '%s', next %s",
                 pump_state_to_string(runtime.pump_state), runtime.reservoir_ok ? "OK" : "LOW",
                 runtime.block_reason[0] ? runtime.block_reason : "none", next);
        history_store_log_diagnostic(msg);
        seen = true;
        last_state = runtime.pump_state;
        last_reservoir = runtime.reservoir_ok;
        snprintf(last_block, sizeof(last_block), "%s", runtime.block_reason);
    }
    cJSON *root = runtime_to_json(&runtime);
    time_t now = time(NULL);
    cJSON_AddNumberToObject(root, "now", (double)now);
    cJSON_AddBoolToObject(root, "time_valid", now > 1700000000);
    cJSON_AddStringToObject(root, "next_scheduled", next);
    return send_json(req, root);
}

static esp_err_t time_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
    const cJSON *epoch = cJSON_GetObjectItem(json, "epoch_sec");
    if (!cJSON_IsNumber(epoch) || epoch->valuedouble < 1700000000) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"valid epoch_sec required\"}");
        return ESP_OK;
    }
    struct timeval tv = {.tv_sec = (time_t)epoch->valuedouble};
    settimeofday(&tv, NULL);
    scheduler_refresh_next_run();
    history_store_log_diagnostic("Controller clock set from browser.");
    cJSON_Delete(json);
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static esp_err_t planters_get(httpd_req_t *req)
{
    planter_config_t planters[PLANTER_COUNT];
    config_store_get_planters(planters);
    if (strstr(req->uri, "/api/v1/planters/")) {
        int id = atoi(strrchr(req->uri, '/') + 1);
        if (id >= 1 && id <= PLANTER_COUNT) return send_json(req, planter_to_json(&planters[id - 1]));
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req, "{\"error\":\"planter not found\"}");
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
    cJSON_AddNumberToObject(root, "updated_at", (double)reading.updated_at);
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
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
    uint32_t duration = 30;
    const cJSON *d = cJSON_GetObjectItem(json, "duration_sec");
    const cJSON *reason = cJSON_GetObjectItem(json, "reason");
    system_settings_t settings;
    config_store_get_settings(&settings);
    if (!cJSON_IsNumber(d) || d->valuedouble < 1 || d->valuedouble > settings.manual_max_run_sec) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        cJSON *err = cJSON_CreateObject();
        char message[96];
        snprintf(message, sizeof(message), "duration_sec must be between 1 and %lu", (unsigned long)settings.manual_max_run_sec);
        cJSON_AddStringToObject(err, "error", message);
        return send_json(req, err);
    }
    duration = (uint32_t)d->valuedouble;
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
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
    system_settings_t settings;
    config_store_get_settings(&settings);
    bool weather_was_enabled = settings.weather_adjust_enabled;
    bool weather_skip_was_enabled = settings.weather_skip_on_rain;
    json_to_settings(json, &settings);
    config_store_save_settings(&settings);
    if (settings.weather_adjust_enabled &&
        (!weather_was_enabled || weather_skip_was_enabled != settings.weather_skip_on_rain)) {
        weather_manager_request_refresh();
    }
    scheduler_refresh_next_run();
    cJSON_Delete(json);
    return settings_get(req);
}

static esp_err_t weather_refresh_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    system_settings_t settings;
    config_store_get_settings(&settings);
    if (!settings.weather_adjust_enabled) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"error\":\"Enable weather adjustment before refreshing the forecast.\"}");
    }
    weather_manager_request_refresh();
    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    return send_json(req, ok);
}

static esp_err_t observation_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
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
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
    const cJSON *hours = cJSON_GetObjectItem(json, "hours");
    if (!cJSON_IsNumber(hours) || hours->valuedouble < 0 || hours->valuedouble > 168) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"hours must be between 0 and 168\"}");
    }
    system_settings_t settings;
    config_store_get_settings(&settings);
    settings.rain_delay_enabled = hours->valueint > 0;
    settings.rain_delay_until = settings.rain_delay_enabled ? time(NULL) + hours->valueint * 3600 : 0;
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
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;

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
        cJSON *json = read_json_body(req);
        if (!json) return ESP_OK;
        planter_config_t planters[PLANTER_COUNT];
        config_store_get_planters(planters);
        if (id >= 1 && id <= PLANTER_COUNT) {
            json_to_planter(json, &planters[id - 1]);
            config_store_save_planter(&planters[id - 1]);
        } else {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "404 Not Found");
            return httpd_resp_sendstr(req, "{\"error\":\"planter not found\"}");
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
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "{\"error\":\"endpoint not found\"}");
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
    reg(server, "/api/v1/time", HTTP_POST, time_post);
    reg(server, "/api/v1/settings", HTTP_POST, settings_post);
    reg(server, "/api/v1/weather/refresh", HTTP_POST, weather_refresh_post);
    reg(server, "/api/v1/schedule", HTTP_POST, settings_post);
    reg(server, "/api/v1/observations", HTTP_POST, observation_post);
    reg(server, "/api/v1/rain-delay", HTTP_POST, rain_delay_post);
    reg(server, "/api/v1/wifi", HTTP_POST, wifi_post);
    reg(server, "/api/v1/planters/*", HTTP_POST, wildcard_post);
    reg(server, "/api/v1/recommendations/*", HTTP_POST, wildcard_post);
    reg(server, "/api/v1/ota/apply", HTTP_POST, ota_apply_post);
    return ESP_OK;
}
