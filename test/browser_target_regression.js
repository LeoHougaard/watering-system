// Evaluate only on the read-only preview; all writes below are mocked.
(async () => {
  if (location.hostname !== '127.0.0.1') throw new Error('Use the read-only preview');
  const results = [], originalFetch = window.fetch;
  const saved = {settings, status, scheduleDirty, scheduleDraft};
  const check = (condition, name) => {if (!condition) throw new Error(name); results.push(name);};
  try {
    while (refreshBusy) await new Promise(r => setTimeout(r, 25));
    refreshBusy = true;
    settings = structuredClone(settings); status = structuredClone(status);
    settings.moisture_target_percent = null; settings.moisture_target_time = '06:00';
    scheduleDirty = false; render();
    const oldSlots = JSON.stringify(settings.schedule_slots);
    check(document.querySelectorAll('#targetPercent').length <= 1, 'Status has no duplicate target form');
    check(schedule.querySelectorAll('[onclick="saveSchedule()"]').length === 1, 'One Save button in Schedule');
    check(schedule.querySelectorAll('h2').length === 1 && schedule.querySelectorAll('p').length === 0, 'No repeated headings or explanatory paragraphs');
    const posted = [];
    window.fetch = async (url, options = {}) => {
      const body = JSON.parse(options.body); posted.push({url, body});
      return new Response(JSON.stringify({...settings,...body}), {headers: {'Content-Type':'application/json'}});
    };
    wateringMode.value = 'target'; changeScheduleMode();
    check(posted.length === 0 && settings.moisture_target_percent === null, 'Changing mode is a draft until Save');
    check(targetPercent.value === '' && dailyTime.value === '06:00', 'Schedule exposes percentage and daily time without choosing a target');
    for (const value of ['', '0', '100', '40.5']) {
      targetPercent.value = value; await saveSchedule();
      check(posted.length === 0, `Invalid percentage ${value || 'empty'} cannot be saved`);
    }
    targetPercent.value = '42'; dailyTime.value = ''; await saveSchedule();
    check(posted.length === 0, 'An empty daily time cannot be saved');
    dailyTime.value = '07:15'; captureSchedule();
    render(); render(); renderDash(true);
    check(targetPercent.value === '42' && dailyTime.value === '07:15', 'Rendering and live readings preserve percentage and time edits');
    wateringMode.value = 'timed'; changeScheduleMode();
    check(JSON.stringify(scheduleDraft.slots) === oldSlots, 'Original weekday schedule remains available');
    wateringMode.value = 'target'; changeScheduleMode();
    check(targetPercent.value === '42' && dailyTime.value === '07:15', 'Mode switching preserves target edits');
    await saveSchedule();
    check(posted.length === 1 && posted[0].body.moisture_target_percent === 42 && posted[0].body.moisture_target_time === '07:15' && Object.keys(posted[0].body).length === 2, 'Save sends only changed percentage and time');
    check(settings.moisture_target_percent === 42 && !scheduleDirty, 'Save adopts the confirmed controller settings');
    render(); await new Promise(r=>requestAnimationFrame(r));drawOne('dashMoistCanvas');
    check(document.querySelector('#dashMoistCanvas').getAttribute('aria-label').includes('Target 42 percent'), 'Graph identifies the target accessibly');
    check(!document.querySelector('.chartSub'), 'Graph has no subtitle');
    check([...document.querySelectorAll('.chartY span')].some(e => parseInt(e.textContent) <= 42), 'Graph range includes the target');
    status.current_run_remaining_sec = 0; status.pump_state = 'IDLE';
    status.moisture_target_cycle_active = true; livePause=0;liveNote();
    check(!pumpControls.hidden && activity.innerText.includes('Soaking'), 'Stop stays available during soaking');
    render(); dailyTime.value = '08:20'; await saveSchedule();
    check(JSON.stringify(posted.at(-1).body) === '{"moisture_target_time":"08:20"}', 'Time can be changed independently in Schedule');
    wateringMode.value = 'timed'; changeScheduleMode(); await saveSchedule();
    check(settings.moisture_target_percent === null && JSON.stringify(settings.schedule_slots) === oldSlots, 'Timed mode clears the target and restores the original weekdays');
    wateringMode.value = 'target'; changeScheduleMode(); targetPercent.value = '55';
    wateringMode.value = 'off'; changeScheduleMode(); await saveSchedule();
    check(settings.auto_mode_enabled === false && !schedule.querySelector('#targetPercent'), 'Off pauses automation without extra controls');
    wateringMode.value = 'target'; changeScheduleMode(); targetPercent.value = '55';
    window.fetch = async () => {throw new TypeError('Simulated disconnect');};
    await saveSchedule();
    check(scheduleDirty && targetPercent.value === '55' && settings.moisture_target_percent === null, 'Failed Save preserves the draft without changing confirmed settings');
    return results;
  } finally {
    window.fetch = originalFetch; settings = saved.settings; status = saved.status;
    scheduleDirty = saved.scheduleDirty; scheduleDraft = saved.scheduleDraft;
    refreshBusy = false; render(); await refresh();
  }
})()
