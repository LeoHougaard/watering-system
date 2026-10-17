# Watering system data and check report

Collected October 16, 2026 at about 20:00 PDT, October 17 at 03:00 UTC. The controller is online and logging reliably. The moisture probe needs a physical check after abnormal readings on October 14 and 15. The optional camera could not be reached by its documented hostname.

The follow-up [weather comparison](weather-comparison.md) checks whether North Vancouver rain explains the readings. The longer trend is plausible; the October 13 to 15 collapse remains unexplained by ordinary drying.

## Controller checks

- Pump IDLE, relay GPIO5 at its off level, no fault flags or block reason, and no active target cycle.
- Clock within one second of this computer at collection. Automation enabled with a 50% target, daily check at 06:00 America/Vancouver, and a 600-second daily cap. The stored weekday schedule is inactive while target mode is selected.
- Latest live reading 94%, raw 1305, updated 49 seconds before the final status snapshot. The last stored hourly reading was 109 seconds old.
- Flash history uses 115,209 of 836,081 bytes, 13.78%.
- All 41 daily checks from September 6 through October 16 are present within 30 seconds of 06:00. Thirty-six days were skipped at the target threshold. Five days watered for ten minutes each, 50 minutes total. Every scheduled pulse completed and every day stayed within its cap.
- The recent 984 hourly records are chronological, unique, and separated by 3,599 to 3,601 seconds. No missing hourly intervals were found in this 41-day export. All 960 records from an independent 40-day request match the overlap exactly.
- Settings were identical before and after collection. This review used GET requests. Firmware, time, calibration, watering settings, and pump commands were not changed.

These checks establish the controller's reported state and recording behavior. Water flow, emitter output, reservoir contents, probe placement, power-cycle recovery, and access from a second physical device were not tested.

## Findings that need attention

The probe recorded 32 zero-percent hourly readings between October 14 at 01:58 and October 15 at 12:58 PDT. Seventeen readings reached raw ADC full scale, 4095. It had been near 90% before the sudden drop and returned above 90% afterward. Check the sensor connector, wiring, and placement. A connection or probe problem is plausible, but the logs cannot identify the cause or prove actual soil moisture. The default dry/wet calibration remains 3000/1200, so these percentages are a relative probe scale.

The controller used its bounded fallback allowance on September 20, 21, 22 and October 14, 15 after detecting insufficient probe response. Each day stayed within ten minutes. A completed timer does not demonstrate successful water delivery.

The reservoir sensor is disabled. `reservoir_ok=true` therefore does not confirm that the tank has water.

The optional camera's `http://esp32-cam.local/status` request failed with `curl: (6) Could not resolve host: esp32-cam.local`. This does not distinguish an offline camera from a hostname-resolution problem. Its existing public firmware source also contains hardcoded network credentials and shared OTA PINs. Their values are excluded from this export. Rotate the exposed credentials and remove them from source before relying on that camera's access controls.

## Data files and limits

The JSON responses are saved byte for byte. [manifest.json](manifest.json) records each endpoint, request time, byte count and SHA-256 checksum. [SHA256SUMS](SHA256SUMS) covers the archive files except itself. A directory-local `.gitattributes` preserves their exact bytes through Git checkout and upload.

- [moisture-recent-41-days.json](moisture-recent-41-days.json) contains 984 unaggregated hourly records, September 5 at 20:59 through October 16 at 19:58 PDT. These are the sensor's normal stored averages, not the underlying one-minute samples.
- [moisture-recent-40-days.json](moisture-recent-40-days.json) contains the 960-record overlap used to verify the recent export. It is not additional distinct data.
- [moisture-retained-detailed.json](moisture-retained-detailed.json) contains 983 time-bucket averages covering the retained period, August 18 through October 16 PDT. This query uses a 60-day window and a 1,000-point cap.
- [moisture-retained-365-days.json](moisture-retained-365-days.json) contains the standard year-window export, 162 coarser averages. Bucket timestamps and values are averages, so the first and last timestamps are not the oldest and newest original readings.
- [watering-history.json](watering-history.json) contains all 66 currently exposed events, July 22 through October 16 PDT: 7 manual, 23 scheduled, and 36 skips. The three incomplete manual events were explicitly stopped in July or August; all 23 scheduled pulses completed.
- [diagnostics.json](diagnostics.json) contains all 145 currently exposed diagnostic records. Old boot records include timestamps near Unix epoch before clock synchronization. They remain unchanged. Two historical OTA failures are followed by later successful updates and are not current fault flags.
- [earlier-records-2026-09-05.json](earlier-records-2026-09-05.json) preserves 429 readings, 19 watering events, 7 planter profiles and 1 observation from the existing September 5 local archive. Those watering events overlap the live history. Planter and observation APIs are no longer exposed by the current firmware.
- [daily-moisture.json](daily-moisture.json) and [daily-automation.json](daily-automation.json) are derived summaries in PDT. The first and last moisture days are partial.
- The remaining files preserve current settings, live readings, status before and after collection, [verification.json](verification.json), and [host-tests.json](host-tests.json).

The firmware caps moisture responses at 1,000 points and averages larger requests. It has no raw-file download, start/end filter, or pagination endpoint. The original 429 archived readings plus 984 recent records preserve 1,413 distinct unaggregated readings. An 8.63-hour interval between those exports is represented by the retained averaged data. This is a limit of the available unaggregated export, not evidence of a sensor outage. This backup is not a complete raw flash image. Historical watering and diagnostic logs can rotate, so only their currently retained records are available.

## Software verification

All four existing host suites passed: model, daily target scheduler, pump controller, and reservoir manager. They exercise local source commit `08b6fc5` with simulated hardware, including daily timing, wet-soil skips, missing-probe fallback, runtime limits, Stop, clock corrections, cooldown, and reservoir configuration.

The live HTML returned HTTP 200 and exactly matched the local HTML source, SHA-256 `f87ddfca69e0d2452160a89ef99780db5fa3c648bc3ad32bd9455ef34e7eaed4`. The status API does not expose a firmware version or binary hash, so full binary identity is unverified. Browser snapshots timed out twice, browser evaluation also timed out, and the fallback browser provider was unavailable. The rendered interface could not be inspected.

Only this data directory is added on the GitHub data branch. The older unpublished firmware/UI commit in the working checkout remains separate.
