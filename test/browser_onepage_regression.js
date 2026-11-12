// Run only on the read-only preview. All network requests here are simulated.
(async()=>{
  if(location.hostname!=='127.0.0.1')throw Error('Use the read-only preview');
  while(refreshBusy)await new Promise(r=>setTimeout(r,25));
  const saved={settings,status,latest,moisture,wateringHistory,scheduleDraft,scheduleDirty,runDay,runMonth},originalFetch=fetch,results=[];
  const check=(ok,name)=>{if(!ok)throw Error(name);results.push(name)};
  try{
    refreshBusy=true;settings=structuredClone(settings);status=structuredClone(status);
    scheduleDirty=false;scheduleDraft=cloneSchedule();render();livePause=0;liveNote();
    check(!document.querySelector('nav')&&manual.closest('#dashGrid')&&schedule.offsetHeight>0,'One page has manual controls beside pump information and a visible schedule');
    check(connection.textContent==='Connected'&&activity.hidden,'Idle header says Connected without redundant activity text');
    runDay='';document.getElementById('wateringHistory').innerHTML=historyBody();
    check(runDetail.hidden,'Calendar details stay hidden until a day is selected');
    check(settingsPanel===document.querySelector('main').lastElementChild,'Settings is the last section');
    check(moistureHistory.tagName==='SECTION'&&!moistureHistory.querySelector('summary')&&document.getElementById('dashMoistCanvas').getBoundingClientRect().height>0,'Moisture graph is visible without a dropdown');
    check(moistureHistory.nextElementSibling.classList.contains('wateringGrid'),'Calendar and schedule follow the moisture graph');
    const calendarBox=document.querySelector('.calendarPanel').getBoundingClientRect(),scheduleBox=schedule.getBoundingClientRect();
    check(innerWidth<=760||Math.abs(calendarBox.height-scheduleBox.height)<1,'Calendar and schedule have equal height side by side');
    manualSec.value='120';manualSec.dispatchEvent(new Event('change',{bubbles:true}));
    settingsPanel.open=true;dryRaw.value='3100';dryRaw.dispatchEvent(new Event('input',{bubbles:true}));
    check(calibration.tagName==='DIV'&&!calibration.querySelector('summary')&&dryRaw.getBoundingClientRect().height>0,'Calibration fields are visible directly in Settings');
    wateringMode.value='target';changeScheduleMode();targetPercent.value='47';dailyTime.value='07:20';captureSchedule();
    const input=targetPercent;input.focus();
    window.fetch=async url=>new Response(JSON.stringify(String(url).endsWith('/status')?{...status,current_run_remaining_sec:90,pump_state:'MANUAL_RUNNING'}:String(url).endsWith('/settings')?settings:String(url).endsWith('/history')?wateringHistory:String(url).endsWith('/latest')?{...latest,percent:64}:moisture),{headers:{'Content-Type':'application/json'}});
    refreshBusy=false;await refresh(true);refreshBusy=true;
    check(targetPercent===input&&targetPercent.value==='47'&&dailyTime.value==='07:20','Full refresh preserves the focused schedule draft');
    check(manualSec.value==='120'&&dryRaw.value==='3100'&&calibration.offsetHeight>0&&settingsPanel.open,'Full refresh preserves manual duration and open calibration edits');
    check(moistureValue.textContent==='64%'&&activity.textContent.includes('1 minute and 30 seconds'),'Live readings and header countdown update while forms are being edited');
    check(!pumpControls.hidden&&manual.querySelector('.run').hidden,'Watering replaces Run with Stop inside the pump card');
    status.current_run_remaining_sec=0;status.pump_state='IDLE';status.moisture_target_cycle_active=true;liveNote();
    check(!pumpControls.hidden&&activity.textContent.includes('Soaking'),'Soak activity appears in the header and can be stopped');
    status.moisture_target_cycle_active=false;status.block_reason='Pump is cooling down.';liveNote();
    check(activity.textContent==='Pump is cooling down.','Current blocking information appears in the header');
    status.block_reason='';liveNote();note('Saved.','ok');
    check(notice.closest('header')&&activity.hidden,'Action feedback appears once in the header');
    livePause=0;liveNote();
    check(!notice.classList.contains('show')&&activity.hidden&&pumpControls.hidden,'Finished feedback clears and idle controls stay simple');
    connected=false;updateControls();
    check(connection.textContent==='Disconnected'&&pumpState.textContent==='Unknown'&&manual.querySelector('.run').disabled,'Disconnected page reports unknown pump state and disables Run');
    return results;
  }finally{
    window.fetch=originalFetch;({settings,status,latest,moisture,wateringHistory,scheduleDraft,scheduleDirty,runDay,runMonth}=saved);
    refreshBusy=false;document.activeElement.blur();clearDirty('prefs');clearDirty('manual');settingsPanel.open=false;livePause=0;markConnected();render();await refresh();
  }
})()
