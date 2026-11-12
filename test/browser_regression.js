// Evaluate on test/preview_server.py in the shared browser, never on the controller.
(async () => {
  if (location.hostname !== '127.0.0.1') throw new Error('Use the read-only preview');
  const results = [], dash = document.querySelector('main');
  const check = (condition, name) => {
    if (!condition) throw new Error(name);
    results.push(name);
  };
  const originalFetch = window.fetch;
  const saved = {status, settings, moisture, wateringHistory, runMonth, runDay, graphDays};
  while (refreshBusy) await new Promise(r => setTimeout(r, 25));
  const context = document.querySelector('#dashMoistCanvas').getContext('2d');
  const originalCurve = context.bezierCurveTo;
  const originalArc = context.arc;
  try {
    while (refreshBusy) await new Promise(r => setTimeout(r, 25));
    // Freeze background polling while testing controlled state. The preview also
    // rejects every POST, independently of these browser mocks.
    refreshBusy = true;
    settings = structuredClone(settings);
    status = structuredClone(status);
    render();
    check(reservoirText() === 'Water level not monitored', 'Disabled reservoir is not reported safe');
    const nowDate = new Date(), testMonth = new Date(nowDate.getFullYear(),nowDate.getMonth()-1,1);
    const stamp = (day,hour=6) => new Date(testMonth.getFullYear(),testMonth.getMonth(),day,hour).getTime()/1000;
    runMonth = testMonth.getFullYear()*12+testMonth.getMonth(); runDay = '';
    wateringHistory = [
      {timestamp:stamp(28),type:'skipped',duration_sec:600,completed:false,stopped_reason:'moisture_target'},
      {timestamp:stamp(15,8),type:'manual',duration_sec:10,completed:false,stopped_reason:'manual_stop'},
      {timestamp:stamp(15),type:'scheduled',duration_sec:600,completed:true,stopped_reason:''},
      {timestamp:stamp(1),type:'scheduled',duration_sec:120,completed:true,stopped_reason:''}
    ];
    render();
    const dayButton = day => dash.querySelector(`[data-run-day="${runDateKey(new Date(stamp(day)*1000))}"]`);
    check(dash.querySelectorAll('.calendarDay').length === new Date(testMonth.getFullYear(),testMonth.getMonth()+1,0).getDate(), 'Calendar has the correct number of days');
    check([...dayButton(1).parentElement.children].indexOf(dayButton(1)) === 7+testMonth.getDay(), 'Calendar starts in the correct weekday column');
    check(dayButton(15).querySelectorAll('.runDot').length === 2, 'Multiple outcomes on one day have separate dots');
    check(!dayButton(2).querySelector('.runDot'), 'Days without records have no watering dot');
    dayButton(28).click();
    check(dayButton(28).querySelector('.runDot.skipped') && !runDetail.textContent.includes('10 minutes'), 'Skipped day shows a skipped dot without implying watering duration');
    dayButton(15).click();
    check(runDetail.textContent.includes('10 seconds') && runDetail.textContent.includes('10 minutes') && runDetail.textContent.includes('Stopped manually'), 'Selecting a day shows every run with actual duration and outcome');
    check(dayButton(15).getAttribute('aria-label').includes('1 completed, 1 stopped') && dayButton(15).getAttribute('aria-pressed') === 'true', 'Calendar days expose outcomes and selection accessibly');
    dayButton(2).click();
    check(runDetail.textContent.includes('No watering recorded.'), 'Selecting a day without records reports that plainly');
    changeRunMonth(1);
    check(dash.querySelector('[aria-label="Next month"]').disabled, 'Calendar stops at the current month');
    changeRunMonth(-1);
    check(dayButton(15).querySelectorAll('.runDot').length === 2 && dash.querySelector('[aria-label="Previous month"]').disabled, 'Previous month restores records and stops at earliest history');
    wateringHistory = []; runDay = ''; render();
    check(!dash.querySelector('.runDot:not(.calendarLegend .runDot)') && runDetail.textContent.includes('No watering recorded this month.'), 'Empty history has an empty calendar');
    wateringHistory = saved.wateringHistory; runMonth = saved.runMonth; runDay = saved.runDay; render();
    render();
    check(reservoirStatus.textContent === 'Water level not monitored', 'Water-level monitoring message is in Settings');
    check(!document.body.innerText.toLowerCase().includes('weather') && !prefs.querySelector('.term'), 'Weather and console removed');
    render();
    updateControls();
    check(pumpControls.hidden, 'Idle pump card has no Stop panel');
    check(!manual.querySelector('.run').disabled, 'Idle pump card still offers Run');
    check(activity.hidden && !manual.innerText.includes('Water level not monitored'), 'Unmonitored message is absent from pump controls');
    settings.reservoir_sensor_enabled = true; status.reservoir_bypass = false; status.reservoir_ok = false; updateControls();
    check(!activity.hidden && activity.textContent === 'Water level low', 'A real low-water fault appears in the header');
    settings.reservoir_sensor_enabled = false; status.reservoir_ok = true; updateControls();
    status.pump_state = 'MANUAL_RUNNING';
    status.current_run_remaining_sec = 120; updateControls();
    check(!pumpControls.hidden, 'Stop appears when manual watering starts');
    busy(true);
    check(!document.querySelector('[data-stop]').disabled, 'Stop remains available while a request is pending');
    busy(false);
    status.pump_state = 'SCHEDULED_RUNNING';
    status.current_run_remaining_sec = 120;
    render();
    check(!pumpControls.hidden && activity.innerText.includes('2 minutes'), 'Header countdown and pump Stop appear while watering');
    check([...document.querySelectorAll('.run')].every(b => b.disabled), 'Start buttons disabled while already running');

    const posted = [];
    window.fetch = async (url, options = {}) => {
      posted.push({url, method: options.method, body: options.body});
      return new Response('{"ok":true}', {headers: {'Content-Type':'application/json'}});
    };
    await stop();
    check(posted.length === 1 && posted[0].url === '/api/v1/manual-stop', 'Stop sends only the stop command');
    posted.length = 0;
    window.fetch = async (url, options = {}) => {
      posted.push({url, body: JSON.parse(options.body)});
      return new Response(JSON.stringify(settings), {headers:{'Content-Type':'application/json'}});
    };
    await saveIt('calibration', () => {settings.moisture_dry_raw = settings.moisture_dry_raw - 1;});
    check(posted.length === 1 && Object.keys(posted[0].body).sort().join(',') === 'moisture_dry_raw', 'Calibration save sends only the changed setting');

    window.fetch = async () => {throw new TypeError('Simulated disconnect');};
    refreshBusy = false;
    const refreshVisible = new Function('document', 'return (' + refreshLive.toString() + ')')({hidden:false});
    await refreshVisible();
    refreshBusy = true;
    check(!connected && connection.innerText === 'Disconnected', 'Connection failure changes Connected to Disconnected');
    check(pumpState.innerText === 'Unknown', 'Disconnected pump state is not presented as live');
    check(!pumpControls.hidden && !document.querySelector('[data-stop]').disabled, 'Stop stays available after connection loss during watering');
    markConnected();
    check(connection.innerText === 'Connected', 'Successful contact restores Connected');
    status.pump_state = 'IDLE'; status.current_run_remaining_sec = 0;
    status.moisture_target_cycle_active = true;
    render();
    check(!pumpControls.hidden && activity.textContent.includes('Soaking'), 'Stop and header activity remain available during soaking');
    status.moisture_target_cycle_active = false; updateControls();
    check(pumpControls.hidden, 'Stop panel disappears when the cycle finishes');

    render();

    await new Promise(r=>requestAnimationFrame(r));
    const now = Math.floor(Date.now()/1000);
    for (const [days, interval] of [[1,3600],[7,10800],[30,43200]]) {
      graphDays = days;
      moisture = Array.from({length:6}, (_,i) => ({timestamp:now-(6-i)*interval,percent:40+i*3}));
      const ctx = document.querySelector('#dashMoistCanvas').getContext('2d');
      let curves = 0, dots = 0;
      ctx.bezierCurveTo = function(...args) {curves++; return originalCurve.apply(this,args);};
      ctx.arc = function(...args) {dots++; return originalArc.apply(this,args);};
      drawOne('dashMoistCanvas');
      check(curves === 10, `${days}-day chart connects six regular readings`);
      moisture.splice(1,4);
      curves = 0; dots = 0;
      drawOne('dashMoistCanvas');
      check(curves === 0 && dots >= 2, `${days}-day chart preserves a real gap and draws isolated readings`);
      ctx.bezierCurveTo = originalCurve;
      ctx.arc = originalArc;
    }
    return results;
  } finally {
    window.fetch = originalFetch;
    status=saved.status;settings=saved.settings;moisture=saved.moisture;wateringHistory=saved.wateringHistory;runMonth=saved.runMonth;runDay=saved.runDay;graphDays=saved.graphDays;
    refreshBusy=false;clearDirty('prefs');markConnected();render();await refresh();
  }
})()
