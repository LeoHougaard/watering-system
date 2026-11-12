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
#include "scheduler.h"
#include "wifi_provisioning.h"

static const char *HTML3 =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Watering System</title><style>:root{--bg:#f6f8f8;--panel:#fff;--text:#17252b;--muted:#64748b;--line:#d7e0df;--accent:#079b95;--chartbg:#f4faf9;--green:#188038;--blue:#1d6fd1;--red:#c9352b}body.dark{--bg:#071014;--panel:#0d171d;--text:#f3f7f8;--muted:#9aa8b6;--line:#25333b;--accent:#12b8b2;--blue:#83b6ff;--green:#75d997;--chartbg:#09151a}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1120px;margin:auto;padding:28px 20px}.top{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:24px}h1{margin:0;font-size:28px;letter-spacing:-.03em}h2{margin:0 0 18px;font-size:15px;font-weight:600}.connectionInfo{text-align:right;max-width:55%;font-size:13px;line-height:1.5}.status{color:var(--green);font-weight:600}.status:before{content:'';display:inline-block;width:6px;height:6px;border-radius:50%;background:currentColor;margin-right:7px}.status.offline{color:var(--red)}.muted{color:var(--muted)}.notice{display:none}.notice.show{display:block}.notice.err{color:var(--red)}.notice.ok{color:var(--muted)}button{font:inherit;min-height:42px;border:1px solid var(--line);background:transparent;color:var(--text);border-radius:7px;padding:9px 14px;cursor:pointer}button:disabled{opacity:.4;cursor:default}button:hover:not(:disabled){border-color:var(--accent)}button:focus-visible,summary:focus-visible{outline:2px solid var(--accent);outline-offset:3px}.danger{background:var(--red);border-color:var(--red);color:#fff}.run,[data-save]{background:var(--accent);border-color:var(--accent);color:#fff}.panel,.card{background:var(--panel);border:1px solid va"
"r(--line);border-radius:12px;padding:22px;min-width:0}.panel{margin-bottom:16px}.statusgrid{display:grid;grid-template-columns:1.6fr .85fr 1.1fr;gap:16px;margin-bottom:16px;align-items:stretch}.cardHead{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.cardHead h2{margin:0}.pumpState{font-size:13px;color:var(--muted)}.metric{font-size:40px;font-weight:650;letter-spacing:-.04em;line-height:1.1}.nextValue{font-size:25px;font-weight:600;line-height:1.2}.nextValue small{display:block;color:var(--muted);font-size:13px;font-weight:400;line-height:1.5;margin-top:6px;letter-spacing:0}.manualControls{display:flex;align-items:flex-end;gap:8px}.manualControls label{flex:1;min-width:0;margin:0;font-size:12px;font-weight:400}.manualControls select{margin-top:5px}.manualControls button{margin:0}#pumpControls[hidden]{display:none}.wateringGrid{display:grid;grid-template-columns:1fr 1fr;gap:16px;align-items:stretch}.wateringGrid{margin-bottom:16px}.wateringGrid>.panel{margin-bottom:0}label{display:block;margin:14px 0 6px;color:var(--muted);font-size:14px;font-weight:500}input,select{width:100%;border:1px solid var(--line);border-radius:7px;padding:10px;background:var(--panel);color:var(--text);font:inherit;min-height:42px}input[type=checkbox]{width:auto;min-height:0}.targetFields{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:18px}.scheduleLimits{margin:18px 0}summary{cursor:pointer;font-size:15px;font-weight:600}.scheduleLimits summary{color:var(--muted);font-size:13px;font-weight:400}#moistureGraph{margin-top:22px}#prefs{max-width:520px;margin-top:20px}#prefs>label:first-child{margin-top:0}#reservoirStatus{font-size:13px}.slot{display:grid;grid-template-columns:1fr 1fr;gap:10px;border-top:1px solid var(--line);padding:12px 0"
"}.slot>label:first-child{grid-column:1/-1}.days{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:3px;grid-column:1/-1}.days label{margin:0;text-align:center;font-size:11px}.days input{display:block;margin:0 auto 6px}.slot .danger{justify-self:start}.addtime{margin-top:10px}.calendarHead{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}.calendarHead button{font-size:24px;line-height:1;padding:4px 12px;border:0;color:var(--text)}.calendarHead strong{font-size:15px}.calendarGrid{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:4px}.weekday{text-align:center;color:var(--muted);font-size:12px;padding:4px 0}.calendarDay{min-width:0;min-height:46px;padding:6px 0;border:1px solid transparent;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px}.calendarDay[aria-current=date]{border-color:var(--line)}.calendarDay[aria-pressed=true]{background:color-mix(in srgb,var(--accent) 15%,transparent);border-color:var(--accent)}.runDots{display:flex;gap:3px;height:6px}.runDot{display:inline-block;flex-shrink:0;width:6px;height:6px;border-radius:50%;background:var(--accent)}.runDot.stopped{background:var(--red)}.runDot.skipped{background:var(--muted)}.calendarLegend{display:flex;justify-content:center;gap:16px;margin-top:14px;color:var(--muted);font-size:11px}.calendarLegend span{display:flex;align-items:center;gap:5px}.runDetails{max-height:250px;overflow:auto}.runDetails h3{font-size:14px;margin:20px 0 4px}.runEntry{display:flex;gap:16px;padding:10px 0;border-bottom:1px solid var(--line);font-size:13px}.runEntry:last-child{border:0}.runEntry time{color:var(--muted);white-space:nowrap}.runEntry span{display:block;margin-top:4px;font-size:12px}@media(max-width:760px){.wrap{padding:20px 12px}.top{alig"
"n-items:flex-start;margin-bottom:20px}h1{font-size:23px}.connectionInfo{font-size:12px;max-width:50%}.statusgrid{grid-template-columns:1fr 1fr;gap:12px}.pumpCard{grid-column:1/-1}.wateringGrid{grid-template-columns:1fr;gap:16px}.panel,.card{padding:18px}.metric{font-size:36px}.nextValue{font-size:23px}.calendarDay{min-height:44px}.calendarLegend{gap:14px}.targetFields{gap:10px}.manualControls label{font-size:13px}}@media(max-width:380px){h1{font-size:21px}.top{gap:10px}.connectionInfo{max-width:45%}}</style></head><body><main class=wrap><header class=top><h1>Watering System</h1><div class=connectionInfo><div id=connection class=status role=status>Connecting...</div><div id=activity class=muted aria-live=polite></div><div id=notice class=notice role=status></div></div></header><div id=dashGrid class=statusgrid><section id=manual class='card pumpCard' data-form><div class=cardHead><h2>Pump</h2><span id=pumpState class=pumpState>Unknown</span></div><div class=manualControls><label>Manual watering<select id=manualSec></select></label><button class=run onclick='runManual()'>Run</button><span id=pumpControls hidden><button class=danger data-stop onclick='stop()'>Stop</button></span></div></section><div class=card><h2>Moisture</h2><div id=moistureValue class=metric>--%</div></div><div class=card><h2>Next watering</h2><div id=nextWatering class=nextValue>--</div></div></div><section id=moistureHistory class='panel statusGraph'><h2>Moisture history</h2><div id=moistureGraph></div></section><div class=wateringGrid><section class='panel calendarPanel' aria-label='Watering history'><div id=wateringHistory></div></section><section id=schedule class=panel data-form aria-label='Schedule'></section></div><details id=settingsPanel class=panel><summary>Settings</summary><section id=prefs"
" data-form></section></details></main><script>let status={},settings={},moisture=[],latest={},wateringHistory=[],connected=false,lastContact=0,requestBusy=false,livePause=0,scheduleDirty=false,scheduleDraft=null,refreshBusy=false,graphDays=[1,7,30].includes(+localStorage.graphDays)?+localStorage.graphDays:1,themeChoice=localStorage.themeMode||(localStorage.theme?'dark':'auto');const names=['sun','mon','tue','wed','thu','fri','sat'],labels=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'],themeMedia=matchMedia('(prefers-color-scheme: dark)');applyTheme();if(themeMedia.addEventListener)themeMedia.addEventListener('change',()=>{if(themeChoice=='auto')applyTheme()});function requestPin(){return new Promise(resolve=>{document.getElementById('pinGate')?.remove();let gate=document.createElement('div');gate.id='pinGate';gate.style.cssText='position:fixed;inset:0;z-index:1000;background:#000b;display:grid;place-items:center;padding:20px';gate.innerHTML=`<div class=panel style='width:min(420px,100%);margin:0'><h2>Watering system PIN</h2><label>PIN<input id=pinEntry type=password autocomplete=current-password autocapitalize=none spellcheck=false></label><div style='display:flex;gap:10px;justify-content:flex-end;margin-top:16px'><button id=pinCancel>Cancel</button><button id=pinSubmit>Continue</button></div></div>`;document.body.appendChild(gate);let input=gate.querySelector('#pinEntry'),done=value=>{gate.remove();resolve(value)};gate.querySelector('#pinCancel').onclick=()=>done('');gate.querySelector('#pinSubmit').onclick=()=>done(input.value.trim());input.onkeydown=e=>{if(e.key=='Enter')done(input.value.trim());else if(e.key=='Escape')done('')};input.focus()})}async function api(p,o,retry=true){let r=await fetch(p,{headers:{'Content-Type':'application/json','X-Local-PIN':localStorage.p"
"in||''},signal:AbortSignal.timeout(10000),...(o||{})});if((r.status==401||r.status==403)&&retry){let entered=await requestPin();if(entered){localStorage.pin=entered;return api(p,o,false)}throw new Error('controller PIN required')}let body=r.headers.get('content-type')?.includes('json')?await r.json():await r.text();if(!r.ok)throw new Error(body.error||body||('HTTP '+r.status));return body}async function syncTime(){if(status.time_valid)return;await api('/api/v1/time',{method:'POST',body:JSON.stringify({epoch_sec:Math.floor(Date.now()/1000)})})}function note(t,k=''){livePause=Date.now()+5000;notice.className='notice show '+k;notice.textContent=t;activity.hidden=true}function busy(on){requestBusy=on;document.querySelectorAll('[data-save]').forEach(b=>b.disabled=on);updateControls()}function clearDirty(id='prefs'){let s=document.getElementById(id);if(s)delete s.dataset.dirty}function dirtySection(e){let s=e.target.closest('[data-form]');if(s&&s.id!='schedule')s.dataset.dirty=1}document.addEventListener('input',dirtySection);document.addEventListener('change',dirtySection);function editingSchedule(){return scheduleDirty||schedule.contains(document.activeElement)}function editingForm(id){let s=document.getElementById(id);return s&&(s.dataset.dirty||s.contains(document.activeElement))}async function saveIt(label,fn){try{busy(true);note('Saving '+label+'...');await syncTime();let before=JSON.stringify(settings);fn();let old=JSON.parse(before),patch=Object.fromEntries(Object.entries(settings).filter(([k,v])=>JSON.stringify(v)!==JSON.stringify(old[k])));settings=await api('/api/v1/settings',{method:'POST',body:JSON.stringify(patch)});clearDirty('prefs');note('Saved.','ok');await refresh(true)}catch(e){note('Could not save '+label+': '+e.message,'err')}finally{busy(false)}}async f"
"unction refresh(force=false){if(refreshBusy)return;refreshBusy=true;try{[status,settings,moisture,latest,wateringHistory]=await Promise.all([api('/api/v1/status'),api('/api/v1/settings'),api(moisturePath()),api('/api/v1/moisture/latest'),api('/api/v1/history')]);markConnected();if(!status.time_valid){await syncTime();status=await api('/api/v1/status')}ensureSlots();if(!scheduleDirty)scheduleDraft=cloneSchedule();render();drawGraph();liveNote()}catch(e){connected=false;updateControls()}finally{refreshBusy=false}}async function refreshLive(){if(refreshBusy||document.hidden)return;refreshBusy=true;try{[status,latest]=await Promise.all([api('/api/v1/status'),api('/api/v1/moisture/latest')]);markConnected();renderDash(true);liveNote()}catch(e){connected=false;updateControls()}finally{refreshBusy=false}}function ensureSlots(){if(!settings.schedule_slots)settings.schedule_slots=[{enabled:true,start:settings.watering_windows?.[0]?.start||'06:00',duration_sec:settings.schedule?.duration_sec||120,days:names.slice()}];settings.schedule_slots=settings.schedule_slots.filter(s=>s.enabled&&s.days&&s.days.length&&s.duration_sec>0)}function reservoirText(){return !settings.reservoir_sensor_enabled?'Water level not monitored':status.reservoir_bypass?'Water level check bypassed':status.reservoir_ok?'Water level OK':'Water level low'}function markConnected(){connected=true;lastContact=Date.now();updateControls()}function updateControls(){let running=status.current_run_remaining_sec>0,cycle=status.moisture_target_cycle_active,c=document.getElementById('connection');c.textContent=connected?'Connected':lastContact?'Disconnected':'Connecting...';c.classList.toggle('offline',!connected);c.title=lastContact?'Last contact '+new Date(lastContact).toLocaleTimeString():'';pumpControls.hidden=!runnin"
"g&&!cycle;let state=prettyState();pumpState.textContent=!connected?'Unknown':running?'Watering':cycle?'Soaking':state.charAt(0).toUpperCase()+state.slice(1);let low=settings.reservoir_sensor_enabled&&!status.reservoir_bypass&&!status.reservoir_ok;activity.textContent=!connected?(lastContact?'Last contact '+new Date(lastContact).toLocaleTimeString():''):low?'Water level low':running?'Watering \u00b7 '+durationText(status.current_run_remaining_sec)+' remaining':cycle?'Soaking before the next moisture check':status.time_valid===false?'Waiting for clock':status.block_reason&&status.block_reason!='Running.'?status.block_reason:(status.fault_flags||[]).join(', ');activity.hidden=!activity.textContent||notice.classList.contains('show');let reservoirStatus=document.getElementById('reservoirStatus');if(reservoirStatus)reservoirStatus.textContent=reservoirText();document.querySelectorAll('.run').forEach(b=>{b.hidden=!!running||!!cycle;b.disabled=requestBusy||!connected||status.pump_state!='IDLE'||!status.reservoir_ok});document.getElementById('manualSec').disabled=requestBusy||!!running||!!cycle}function esc(v){return String(v ?? '').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]))}let runMonth=new Date().getFullYear()*12+new Date().getMonth(),runDay='';function runDateKey(date){return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`}function runKind(r){return r.type=='skipped'?'skipped':r.completed?'completed':'stopped'}function runRecords(){return wateringHistory.filter(r=>Number.isFinite(r.timestamp)&&r.timestamp>0).slice().sort((a,b)=>a.timestamp-b.timestamp)}function runOutcome(r){return r.completed?'Completed':({rain_delay:'Rain delay',auto_disabled:'Schedule paused',low_reservo"
"ir:'Low water',cooldown:'Cooldown',manual_stop:'Stopped manually',moisture_target:'Moisture near target'})[r.stopped_reason]||String(r.stopped_reason||'Stopped').replaceAll('_',' ')}function runDayDetails(){if(!runDay)return '<p class=muted>No watering recorded this month.</p>';let rows=runRecords().filter(r=>runDateKey(new Date(r.timestamp*1000))==runDay),date=new Date(runDay+'T12:00:00');return `<h3>${date.toLocaleDateString(undefined,{month:'long',day:'numeric'})}</h3>${rows.length?rows.map(r=>`<div class=runEntry><time>${new Date(r.timestamp*1000).toLocaleTimeString(undefined,{hour:'numeric',minute:'2-digit'})}</time><div>${r.type=='skipped'?'Skipped':`${r.type=='scheduled'?'Scheduled':'Manual'} \u00b7 ${durationText(r.duration_sec)}`}<span class=muted>${esc(runOutcome(r))}</span></div></div>`).join(''):'<p class=muted>No watering recorded.</p>'}`}function historyBody(){let now=new Date(),current=now.getFullYear()*12+now.getMonth(),rows=runRecords(),first=rows.length?new Date(rows[0].timestamp*1000):now,min=Math.min(current,first.getFullYear()*12+first.getMonth());runMonth=Math.max(min,Math.min(current,runMonth));let date=new Date(Math.floor(runMonth/12),runMonth%12,1),prefix=runDateKey(date).slice(0,7),groups={};rows.forEach(r=>{let key=runDateKey(new Date(r.timestamp*1000));(groups[key]||(groups[key]=[])).push(r)});if(!runDay.startsWith(prefix))runDay='';let count=new Date(date.getFullYear(),date.getMonth()+1,0).getDate(),today=runDateKey(now),labels=['Completed','Stopped','Skipped'];return `<div class=runCalendar><div><div class=calendarHead><button type=button aria-label='Previous month' onclick='changeRunMonth(-1)' ${runMonth<=min?'disabled':''}>&#8249;</button><strong>${date.toLocaleDateString(undefined,{month:'long',year:'numeric'})}</strong><button type=button ar"
"ia-label='Next month' onclick='changeRunMonth(1)' ${runMonth>=current?'disabled':''}>&#8250;</button></div><div class=calendarGrid role=group aria-label='Watering calendar'>${['Sun','Mon','Tue','Wed','Thu','Fri','Sat'].map(d=>`<span class=weekday aria-hidden=true>${d}</span>`).join('')}${'<span aria-hidden=true></span>'.repeat(date.getDay())}${Array.from({length:count},(_,i)=>{let day=new Date(date.getFullYear(),date.getMonth(),i+1),key=runDateKey(day),records=groups[key]||[],kinds=[...new Set(records.map(runKind))],summary=labels.map(label=>{let n=records.filter(r=>runKind(r)==label.toLowerCase()).length;return n?`${n} ${label.toLowerCase()}`:''}).filter(Boolean).join(', ');return `<button type=button class=calendarDay data-run-day='${key}' onclick='selectRunDay(this)' aria-pressed='${runDay==key}' ${key==today?\"aria-current=date\":''} ${key>today?'disabled':''} aria-label='${esc(day.toLocaleDateString(undefined,{month:'long',day:'numeric',year:'numeric'}))}: ${summary||'No watering recorded'}'><span>${i+1}</span><span class=runDots aria-hidden=true>${kinds.map(kind=>`<i class='runDot ${kind}'></i>`).join('')}</span></button>`}).join('')}</div><div class=calendarLegend>${labels.map(label=>`<span><i class='runDot ${label.toLowerCase()}'></i>${label}</span>`).join('')}</div></div><div id=runDetail class=runDetails role=status ${runDay?'':'hidden'}>${runDayDetails()}</div></div>`}function changeRunMonth(delta){runMonth+=delta;document.getElementById('wateringHistory').innerHTML=historyBody()}function selectRunDay(button){runDay=button.dataset.runDay;document.querySelectorAll('[data-run-day]').forEach(b=>b.setAttribute('aria-pressed',b==button?'true':'false'));document.getElementById('runDetail').hidden=false;document.getElementById('runDetail').innerHTML=runDayDetails()}fu"
"nction durationText(s){s=Math.max(0,Math.round(s||0));if(!s)return '0 seconds';let units=[[86400,'day'],[3600,'hour'],[60,'minute'],[1,'second']],parts=[];for(let [size,name] of units){let value=Math.floor(s/size);s%=size;if(value)parts.push(`${value} ${name}${value==1?'':'s'}`);if(parts.length==2)break}return parts.join(' and ')}function durationOptions(selected,values){values=[...new Set([...(values||[]),selected])].filter(v=>v>0).sort((a,b)=>a-b);return values.map(v=>`<option value=${v} ${v==selected?'selected':''}>${durationText(v)}</option>`).join('')}function minuteDurations(max=60){return Array.from({length:max},(_,i)=>(i+1)*60)}function prettyState(){return (status.pump_state||'UNKNOWN').replaceAll('_',' ').toLowerCase()}function liveNote(){if(Date.now()>=livePause){notice.className='notice';notice.textContent=''}updateControls()}function renderDash(keep=false){moistureValue.textContent=settings.moisture_sensor_enabled?(latest.percent==null?'--%':latest.percent+'%'):'Off';let next=settings.auto_mode_enabled?(status.next_scheduled||'No schedule'):'Paused',parts=next.split(' in ');nextWatering.innerHTML=esc(parts[0])+(parts[1]?`<small>in ${esc(parts[1])}</small>`:'');let max=Math.max(1,settings.manual_max_run_sec||1200),select=document.getElementById('manualSec');if(select.dataset.max!=max){let value=select.value?Math.min(+select.value,max):Math.min(300,max);select.innerHTML=durationOptions(value,[10,30,...minuteDurations(Math.floor(max/60))].filter(s=>s<=max));select.dataset.max=max}if(!document.getElementById('dashMoistCanvas'))moistureGraph.innerHTML=graphBody('dashMoistCanvas');updateControls()}function render(){renderDash();document.getElementById('wateringHistory').innerHTML=historyBody();if(!document.getElementById('wateringMode')||!editingSchedule())render"
"Schedule();if(!document.getElementById('themeMode')||!editingForm('prefs')){prefs.innerHTML=`<label>Theme<select id=themeMode onchange='saveTheme(this.value)'><option value=auto ${themeChoice=='auto'?'selected':''}>System</option><option value=light ${themeChoice=='light'?'selected':''}>Light</option><option value=dark ${themeChoice=='dark'?'selected':''}>Dark</option></select></label><p id=reservoirStatus class=muted>${reservoirText()}</p><div id=calibration class=scheduleLimits><h2>Calibration</h2><p class=muted>Raw: ${latest.raw||0}</p><label><input type=checkbox id=moistEnabled ${settings.moisture_sensor_enabled?'checked':''}> Sensor enabled</label><label>Dry<input id=dryRaw type=number min=0 max=4095 value='${settings.moisture_dry_raw}'></label><label>Wet<input id=wetRaw type=number min=0 max=4095 value='${settings.moisture_wet_raw}'></label><button data-save onclick='saveSensorSettings()'>Save</button></div>`}updateControls()}function renderSchedule(){let sd=scheduleDirty&&scheduleDraft?scheduleDraft:cloneSchedule();scheduleDraft=sd;let limitsOpen=schedule.querySelector('.scheduleLimits')?.open;schedule.innerHTML=`<h2>Schedule</h2><label>Mode<select id=wateringMode onchange='changeScheduleMode()'><option value=off ${sd.mode=='off'?'selected':''}>Off</option><option value=timed ${sd.mode=='timed'?'selected':''}>Timed</option><option value=target ${sd.mode=='target'?'selected':''}>Moisture</option></select></label>${sd.mode=='off'?'':sd.mode=='target'?`<div class=targetFields><label>Target (%)<input id=targetPercent type=number min=1 max=99 step=1 value='${sd.targetPercent}' oninput='captureSchedule()'></label><label>Daily time<input id=dailyTime type=time value='${sd.targetTime}' oninput='captureSchedule()'></label></div>`:`${sd.slots.map((sl,i)=>slotHtml(sl,i)).jo"
"in('')||'<p>No times set.</p>'}<button class=addtime onclick='addSlot()'>Add time</button>`}<details class=scheduleLimits ${limitsOpen?'open':''}><summary>Limits</summary><label>Maximum runtime<select id=maxSchedSec onchange='captureSchedule()'>${durationOptions(sd.maxRunSec,minuteDurations())}</select></label></details><button data-save onclick='saveSchedule()'>Save</button>`}function cloneSchedule(){return {mode:!settings.auto_mode_enabled?'off':settings.moisture_target_percent==null?'timed':'target',targetPercent:settings.moisture_target_percent ?? '',maxRunSec:settings.scheduled_max_run_sec||300,targetTime:settings.moisture_target_time||settings.schedule_slots?.[0]?.start||'06:00',slots:(settings.schedule_slots||[]).map(s=>({enabled:!!s.enabled,start:s.start||'06:00',duration_sec:s.duration_sec||120,days:[...(s.days||[])]}))}}function captureSchedule(){if(!schedule.querySelector('#maxSchedSec'))return scheduleDraft;let base=scheduleDraft||cloneSchedule();scheduleDraft={mode:wateringMode.value,targetPercent:document.getElementById('targetPercent')?.value ?? base.targetPercent,maxRunSec:Math.max(1,+maxSchedSec.value||300),targetTime:document.getElementById('dailyTime')?.value ?? base.targetTime,slots:base.mode!='timed'?base.slots:base.slots.map((sl,i)=>({enabled:document.getElementById('se'+i).checked,start:document.getElementById('st'+i).value||'06:00',duration_sec:Math.max(1,+document.getElementById('sd'+i).value||60),days:names.filter((n,d)=>document.getElementById('s'+i+'d'+d).checked)}))};scheduleDirty=true;return scheduleDraft}function changeScheduleMode(){captureSchedule();renderSchedule()}function slotHtml(sl,i){return `<div class=slot><label><input type=checkbox id=se${i} ${sl.enabled?'checked':''} onchange='captureSchedule()'> Enabled</label><label>Start<inp"
"ut id=st${i} type=time value='${sl.start||'06:00'}' oninput='captureSchedule()'></label><label>Duration<select id=sd${i} onchange='captureSchedule()'>${durationOptions(sl.duration_sec||120,minuteDurations())}</select></label><div class=days>${names.map((n,d)=>`<label><input type=checkbox id=s${i}d${d} ${(sl.days||[]).includes(n)?'checked':''} onchange='captureSchedule()'>${labels[d]}</label>`).join('')}</div><button class=danger onclick='removeSlot(${i})'>Remove</button></div>`}function addSlot(){let d=captureSchedule()||cloneSchedule();if(d.slots.length>=8)return note('Maximum 8 schedule times.','err');d.slots.push({enabled:true,start:'06:00',duration_sec:120,days:names.slice()});scheduleDraft=d;scheduleDirty=true;renderSchedule()}function removeSlot(i){let d=captureSchedule()||cloneSchedule();d.slots.splice(i,1);scheduleDraft=d;scheduleDirty=true;renderSchedule()}function runManual(){let m=Math.max(1,settings.manual_max_run_sec||1200),v=Math.max(1,Math.min(m,+manualSec.value||60));clearDirty('manual');run(v)}async function run(s){if(!connected)return;try{busy(true);clearDirty('manual');note('Starting pump for '+durationText(s)+'...');await api('/api/v1/manual-run',{method:'POST',body:JSON.stringify({duration_sec:s,reason:'manual_user_request'})});note('Watering.','ok');await refresh()}catch(e){note('Pump command failed: '+e.message,'err')}finally{busy(false)}}async function stop(){try{busy(true);clearDirty('manual');note('Stopping pump...');await api('/api/v1/manual-stop',{method:'POST'});note('Stopped.','ok');await refresh()}catch(e){note('Stop failed: '+e.message,'err')}finally{busy(false)}}async function saveSchedule(){try{let draft=captureSchedule(),desired={auto_mode_enabled:draft.mode!='off',scheduled_max_run_sec:draft.maxRunSec};if(draft.mode=='target'){let tar"
"get=Number(draft.targetPercent);if(draft.targetPercent===''||!Number.isInteger(target)||target<1||target>99)throw Error('Enter a whole percentage from 1 to 99.');if(!/^([01]\\d|2[0-3]):[0-5]\\d$/.test(draft.targetTime))throw Error('Enter a daily time.');desired.moisture_target_percent=target;desired.moisture_target_time=draft.targetTime}else if(draft.mode=='timed'){let compact=draft.slots.filter(s=>s.enabled&&s.days.length&&s.duration_sec>0);while(compact.length<8)compact.push({enabled:false,start:'06:00',duration_sec:120,days:[]});desired.moisture_target_percent=null;desired.scheduled_max_run_sec=Math.max(draft.maxRunSec,...compact.map(s=>s.enabled?s.duration_sec:0));desired.schedule_slots=compact;desired.schedule={...settings.schedule,duration_sec:compact[0]?.duration_sec||settings.schedule.duration_sec,days:compact[0]?.days||settings.schedule.days};desired.watering_windows=[{start:compact[0]?.start||'06:00',end:'09:00'}]}let patch=Object.fromEntries(Object.entries(desired).filter(([k,v])=>JSON.stringify(v)!==JSON.stringify(settings[k])));busy(true);await syncTime();settings=await api('/api/v1/settings',{method:'POST',body:JSON.stringify(patch)});ensureSlots();scheduleDirty=false;scheduleDraft=cloneSchedule();renderSchedule();render();note('Saved.','ok');await refresh(true)}catch(e){note(e.message,'err')}finally{busy(false)}}async function saveSensorSettings(){await saveIt('calibration',()=>{settings.moisture_sensor_enabled=moistEnabled.checked;settings.moisture_dry_raw=+dryRaw.value;settings.moisture_wet_raw=+wetRaw.value})}function applyTheme(v=themeChoice){themeChoice=['auto','light','dark'].includes(v)?v:'auto';document.body.classList.toggle('dark',themeChoice=='dark'||(themeChoice=='auto'&&themeMedia.matches))}function saveTheme(v){localStorage.themeMode=v;localSto"
"rage.removeItem('theme');applyTheme(v);drawGraph();note('Theme saved.','ok')}function liveLoop(){refreshLive().finally(()=>setTimeout(liveLoop,status.current_run_remaining_sec>0?1000:5000))}document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh(true)});</script></body></html>";

static const char *GRAPH_PATCH =
"<style>.statusGraph{padding:24px}.chartHead{align-items:center;margin:0 0 20px}.chartSub{font-size:14px}.chartRanges{gap:0;border:1px solid var(--line);border-radius:9px;overflow:hidden;flex-wrap:nowrap}.chartRanges button{min-height:38px;padding:8px 13px;border:0;border-right:1px solid var(--line);border-radius:0;background:transparent;color:var(--muted);white-space:nowrap}.chartRanges button:last-child{border-right:0}.chartRanges button.on{background:color-mix(in srgb,var(--text) 8%,transparent);color:var(--text)}.chartRanges button:disabled{cursor:wait;opacity:.7}.chartPlot{display:grid;grid-template-columns:48px minmax(0,1fr);grid-template-rows:300px 30px;min-width:0}.chartY,.chartX{position:relative;color:var(--muted);font-size:12px}.chartY span,.chartX span{position:absolute;white-space:nowrap}.chartY span{right:10px;transform:translateY(-50%)}.chartY span:first-child{transform:none}.chartY span:last-child{transform:translateY(-100%)}.chartX span{top:9px;transform:translateX(-50%)}.chartX span:first-child{transform:none}.chartX span:last-child{transform:translateX(-100%)}.chartStage{position:relative;min-width:0;overflow:hidden}.chartStage canvas{position:absolute;inset:0;width:100%;height:100%;border:0;border-radius:0;background:transparent}.chartEmpty{position:absolute;inset:0;display:grid;place-items:center;color:var(--muted);font-size:14px}.chartEmpty[hidden]{display:none}@media(max-width:560px){.statusGraph{padding:16px}.chartHead{display:flex;align-items:flex-start;gap:12px}.chartRanges{margin-top:0;width:100%}.chartRanges button{flex:1;padding:7px 5px;font-size:12px}.chartPlot{grid-template-columns:40px minmax(0,1fr);grid-template-rows:245px 32px}.chartY,.chartX{font-size:11px}.chartY span{right:7px}}</style><style>@media(max-width:560px){.chartHead{display"
":block}.chartRanges{margin-top:12px}}</style><script>function graphAverageText(){return ({1:'hourly readings',7:'3-hour averages',30:'12-hour averages',365:'daily averages'})[graphDays]||'averaged readings'}function moisturePath(){let points=({1:48,7:56,30:60,365:365})[graphDays]||120;return `/api/v1/moisture?days=${graphDays}&max_points=${points}`}function graphBody(id){let ranges=[[1,'24 hours'],[7,'7 days'],[30,'30 days']];return `<div class=chartHead><div class=chartRanges role=group aria-label='History range'>${ranges.map(([d,t])=>`<button type=button data-graph-days=${d} class='${graphDays==d?'on':''}' aria-pressed='${graphDays==d}'>${t}</button>`).join('')}</div></div><div class=chartPlot><div class=chartY aria-hidden=true></div><div class=chartStage><canvas id=${id} aria-label='Moisture history chart'></canvas><div class=chartEmpty hidden>No readings in this range</div></div><div></div><div class=chartX aria-hidden=true></div></div>`}function graphButtonState(loading){document.querySelectorAll('[data-graph-days]').forEach(b=>{let on=+b.dataset.graphDays==graphDays;b.classList.toggle('on',on);b.setAttribute('aria-pressed',on);b.disabled=loading})}async function setGraphRange(days){if(![1,7,30].includes(days))return;let previous=graphDays;graphDays=days;localStorage.graphDays=days;graphButtonState(true);try{moisture=await api(moisturePath());graphButtonState(false);drawGraph()}catch(e){graphDays=previous;localStorage.graphDays=previous;graphButtonState(false);drawGraph();note('Could not load moisture history: '+e.message,'err')}}document.addEventListener('click',e=>{let b=e.target.closest('[data-graph-days]');if(!b)return;e.preventDefault();setGraphRange(+b.dataset.graphDays)});function graphDate(t){let d=new Date(t*1000);return graphDays==1?d.toLocaleTimeString(u"
"ndefined,{hour:'numeric',minute:'2-digit'}):graphDays==365?d.toLocaleDateString(undefined,{month:'short'}):d.toLocaleDateString(undefined,{month:'short',day:'numeric'})}function graphGapSeconds(){let bucket=({1:3600,7:10800,30:43200,365:86400})[graphDays]||3600;return Math.max((settings.moisture_sample_interval_sec||3600)*3,bucket*1.75)}function graphCurve(x,points){x.moveTo(points[0][0],points[0][1]);for(let i=1;i<points.length;i++){let a=points[i-1],b=points[i],control=(b[0]-a[0])*.42;x.bezierCurveTo(a[0]+control,a[1],b[0]-control,b[1],b[0],b[1])}}function drawOne(id){let c=document.getElementById(id);if(!c)return;let panel=c.closest('.statusGraph'),stage=c.parentElement,w=Math.round(stage.clientWidth),h=Math.round(stage.clientHeight);if(w<80||h<80)return;let ratio=Math.min(2,window.devicePixelRatio||1);if(c.width!=Math.round(w*ratio)||c.height!=Math.round(h*ratio)){c.width=Math.round(w*ratio);c.height=Math.round(h*ratio)}let x=c.getContext('2d');x.setTransform(ratio,0,0,ratio,0,0);x.clearRect(0,0,w,h);let now=Math.floor(Date.now()/1000),start=now-graphDays*86400,d=moisture.filter(p=>(p.timestamp||0)>=start&&p.percent!=null).sort((a,b)=>a.timestamp-b.timestamp),yLabels=panel.querySelector('.chartY'),xLabels=panel.querySelector('.chartX'),empty=panel.querySelector('.chartEmpty'),style=getComputedStyle(document.body),grid=style.getPropertyValue('--line').trim()||'#25333b',accent=style.getPropertyValue('--accent').trim()||'#12b8b2';empty.hidden=!!d.length;if(!d.length){yLabels.innerHTML='';xLabels.innerHTML='';return}let values=d.map(p=>+p.percent),current=values.at(-1),average=Math.round(values.reduce((a,b)=>a+b,0)/values.length),low=Math.min(...values),high=Math.max(...values),change=current-values[0],signed=(change>0?'+':'')+change+' points';let yMin=Math.max(0,Math.f"
"loor((Math.min(low,settings.moisture_target_percent ?? low)-4)/5)*5),yMax=Math.min(100,Math.ceil((Math.max(high,settings.moisture_target_percent ?? high)+4)/5)*5);if(yMax-yMin<20){let need=20-(yMax-yMin),down=Math.min(yMin,Math.ceil(need/2));yMin-=down;yMax=Math.min(100,yMax+need-down);if(yMax-yMin<20)yMin=Math.max(0,yMax-20)}let span=yMax-yMin||20;yLabels.innerHTML=Array.from({length:5},(_,i)=>`<span style='top:${i*25}%'>${Math.round(yMax-span*i/4)}%</span>`).join('');xLabels.innerHTML=Array.from({length:5},(_,i)=>`<span style='left:${i*25}%'>${graphDate(start+graphDays*86400*i/4)}</span>`).join('');x.strokeStyle=grid;x.lineWidth=1;x.globalAlpha=.7;for(let i=0;i<5;i++){let gy=Math.round(h*i/4)+.5;x.beginPath();x.moveTo(0,gy);x.lineTo(w,gy);x.stroke()}for(let i=0;i<5;i++){let gx=Math.round(w*i/4)+.5;x.beginPath();x.moveTo(gx,0);x.lineTo(gx,h);x.stroke()}x.globalAlpha=1;let px=t=>(t-start)*w/(graphDays*86400),py=v=>(yMax-v)*h/span,points=d.map(p=>[px(p.timestamp),py(+p.percent),p.timestamp]),gap=graphGapSeconds(),segments=[];points.forEach((p,i)=>{if(!i||p[2]-points[i-1][2]>gap)segments.push([p]);else segments.at(-1).push(p)});if(settings.moisture_target_percent!=null){x.strokeStyle=accent;x.setLineDash([6,4]);x.beginPath();x.moveTo(0,py(settings.moisture_target_percent));x.lineTo(w,py(settings.moisture_target_percent));x.stroke();x.setLineDash([])}let fill=x.createLinearGradient(0,0,0,h);fill.addColorStop(0,'rgba(18,184,178,.20)');fill.addColorStop(1,'rgba(18,184,178,.015)');segments.forEach(s=>{x.beginPath();graphCurve(x,s);x.lineTo(s.at(-1)[0],h);x.lineTo(s[0][0],h);x.closePath();x.fillStyle=fill;x.fill()});x.strokeStyle=accent;x.lineWidth=2.6;x.lineCap='round';x.lineJoin='round';segments.forEach(s=>{x.beginPath();if(s.length==1){x.fillStyle=accent;x.arc(s[0][0],s[0]["
"1],3,0,Math.PI*2);x.fill()}else{graphCurve(x,s);x.stroke()}});let last=points.at(-1);x.fillStyle=accent;x.beginPath();x.arc(last[0],last[1],3.5,0,Math.PI*2);x.fill();c.setAttribute('aria-label',`Moisture history. Latest saved ${current} percent, average ${average} percent, low ${low} percent, high ${high} percent.${settings.moisture_target_percent==null?'':` Target ${settings.moisture_target_percent} percent.`}`)}function drawGraph(){drawOne('dashMoistCanvas')}let graphResizeTimer;addEventListener('resize',()=>{clearTimeout(graphResizeTimer);graphResizeTimer=setTimeout(drawGraph,80)});setInterval(refresh,60000);refresh();liveLoop();</script>";

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
    if (!config_store_pin_matches(pin)) {
        vTaskDelay(pdMS_TO_TICKS(250));
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"invalid local PIN\"}");
        return false;
    }
    return true;
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const char *body_end = strstr(HTML3, "</body>");
    if (!body_end) return httpd_resp_sendstr(req, HTML3);
    httpd_resp_send_chunk(req, HTML3, body_end - HTML3);
    httpd_resp_sendstr_chunk(req, GRAPH_PATCH);
    esp_err_t err = httpd_resp_send_chunk(req, body_end, HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(req, NULL, 0);
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
    cJSON_AddStringToObject(root, "moisture_target_status", scheduler_target_status());
    cJSON_AddBoolToObject(root, "moisture_target_cycle_active", scheduler_target_cycle_active());
    size_t storage_total = 0;
    size_t storage_used = 0;
    if (history_store_storage_info(&storage_total, &storage_used) == ESP_OK) {
        cJSON_AddNumberToObject(root, "storage_total_bytes", (double)storage_total);
        cJSON_AddNumberToObject(root, "storage_used_bytes", (double)storage_used);
    }
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

static esp_err_t settings_get(httpd_req_t *req)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    cJSON *json = settings_to_json(&settings);
    int target = config_store_moisture_target();
    if (target) cJSON_AddNumberToObject(json, "moisture_target_percent", target);
    else cJSON_AddNullToObject(json, "moisture_target_percent");
    unsigned minute = (unsigned)config_store_target_time() % 1440;
    char daily_time[6];
    snprintf(daily_time, sizeof(daily_time), "%02u:%02u", minute / 60, minute % 60);
    cJSON_AddStringToObject(json, "moisture_target_time", daily_time);
    return send_json(req, json);
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
    uint32_t days = 365;
    size_t max_points = 1000;
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0 && query_len < 128) {
        char query[128];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char value[16];
            if (httpd_query_key_value(query, "days", value, sizeof(value)) == ESP_OK) {
                days = (uint32_t)strtoul(value, NULL, 10);
            }
            if (httpd_query_key_value(query, "max_points", value, sizeof(value)) == ESP_OK) {
                max_points = (size_t)strtoul(value, NULL, 10);
            }
        }
    }
    char *text = history_store_read_moisture_json(days, max_points);
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
    cJSON_AddNumberToObject(root, "samples_collected", reading.samples_collected);
    cJSON_AddNumberToObject(root, "updated_at", (double)reading.updated_at);
    if (reading.percent >= 0) cJSON_AddNumberToObject(root, "percent", reading.percent);
    else cJSON_AddNullToObject(root, "percent");
    return send_json(req, root);
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
    scheduler_cancel_target_cycle();
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
    scheduler_cancel_target_cycle();
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
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(json, "moisture_target_percent");
    const cJSON *daily_time = cJSON_GetObjectItemCaseSensitive(json, "moisture_target_time");
    int daily_minute = config_store_target_time();
    if (daily_time) {
        const char *value = cJSON_GetStringValue(daily_time);
        if (!value || strlen(value) != 5 || value[2] != ':' ||
            value[0] < '0' || value[0] > '2' || value[1] < '0' || value[1] > '9' ||
            value[3] < '0' || value[3] > '5' || value[4] < '0' || value[4] > '9' ||
            (value[0] == '2' && value[1] > '3')) {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"error\":\"Enter a time from 00:00 to 23:59.\"}");
        }
        daily_minute = (value[0] - '0') * 600 + (value[1] - '0') * 60 +
            (value[3] - '0') * 10 + value[4] - '0';
    }
    if (target && !cJSON_IsNull(target) &&
        (!cJSON_IsNumber(target) || target->valuedouble < 1 || target->valuedouble > 99 ||
         target->valuedouble != target->valueint)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"Target must be a whole percentage from 1 to 99, or null for schedule only.\"}");
    }
    json_to_settings(json, &settings);
    if (target && cJSON_IsNumber(target) && !settings.moisture_sensor_enabled) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"Enable the moisture probe before setting a target.\"}");
    }
    if (daily_time) {
        if (config_store_set_target_time(daily_minute) != ESP_OK) {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"error\":\"Could not save daily time.\"}");
        }
    }
    if (target) {
        esp_err_t err = config_store_set_moisture_target(cJSON_IsNull(target) ? 0 : target->valueint);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"error\":\"Could not save moisture target.\"}");
        }
    }
    if (target || daily_time || cJSON_HasObjectItem(json, "schedule_slots") ||
        cJSON_HasObjectItem(json, "auto_mode_enabled") || cJSON_HasObjectItem(json, "scheduled_max_run_sec")) {
        scheduler_cancel_target_cycle();
    }
    if (config_store_save_settings(&settings) != ESP_OK) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"Could not save settings.\"}");
    }
    if (!settings.auto_mode_enabled && pump_controller_get_runtime().pump_state == PUMP_STATE_SCHEDULED_RUNNING) {
        pump_controller_stop(STOP_AUTO_DISABLED);
    }
    scheduler_refresh_next_run();
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

static esp_err_t pin_post(httpd_req_t *req)
{
    if (!require_pin(req)) return ESP_OK;
    cJSON *json = read_json_body(req);
    if (!json) return ESP_OK;
    cJSON *pin = cJSON_GetObjectItemCaseSensitive(json, "pin");
    esp_err_t err = cJSON_IsString(pin) ? config_store_set_pin(pin->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"PIN must be 4 to 31 characters\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
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
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 120;
    config.stack_size = 8192;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    reg(server, "/", HTTP_GET, index_get);
    reg(server, "/api/v1/status", HTTP_GET, status_get);
    reg(server, "/api/v1/settings", HTTP_GET, settings_get);
    reg(server, "/api/v1/schedule", HTTP_GET, settings_get);
    reg(server, "/api/v1/history", HTTP_GET, history_get);
    reg(server, "/api/v1/moisture", HTTP_GET, moisture_get);
    reg(server, "/api/v1/moisture/latest", HTTP_GET, moisture_latest_get);
    reg(server, "/api/v1/diagnostics", HTTP_GET, diagnostics_get);
    reg(server, "/api/v1/wifi", HTTP_GET, wifi_get);
    reg(server, "/api/v1/manual-run", HTTP_POST, manual_run_post);
    reg(server, "/api/v1/manual-stop", HTTP_POST, stop_post);
    reg(server, "/api/v1/time", HTTP_POST, time_post);
    reg(server, "/api/v1/settings", HTTP_POST, settings_post);
    reg(server, "/api/v1/schedule", HTTP_POST, settings_post);
    reg(server, "/api/v1/wifi", HTTP_POST, wifi_post);
    reg(server, "/api/v1/admin/pin", HTTP_POST, pin_post);
    reg(server, "/api/v1/ota/apply", HTTP_POST, ota_apply_post);
    return ESP_OK;
}
