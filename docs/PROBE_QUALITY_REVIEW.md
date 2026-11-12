# Probe quality correction, 2026-11-12

Hardware cause remains unresolved. The October 13–15 collapse and recovery do not establish a failed sensor. The earlier audit thread and weather comparison are dated evidence, not a current device check. No device endpoints were contacted for this correction.

## Source provenance

Canonical source: `C:/Users/Leo/OneDrive/Projects/watering-system`, clean starting revision `08b6fc5` (September daily target implementation). Its parent and GitHub main at inspection were `5689202c68d88097f40a9754b74125a1b88840fd`.

Read-only comparison: `C:/Users/Leo/OneDrive/Projects/Watering system`, HEAD `11db77d81b37d0c205d78d618b392a8cfc913e34`, eight tracked modifications: `agend.md`, API server, config store, irrigation model header/implementation, moisture manager, and status display CMake/implementation. That copy uses a different live sampling/history implementation and includes OLED work absent from canonical source. Neither copy was overwritten, merged or deleted; this correction applies only to canonical source. Local connection notes differ and are deliberately not reproduced here. No assumptions about which firmware is physically installed follow from these checkouts.

Publication uses a source-only baseline of `08b6fc5` on top of GitHub main, excluding `docs/reviews/` saved telemetry and captures. The correction initially targeted that baseline. Final delivery targets `main` and includes this required canonical daily-target source dependency, preserving the original local history. This is source delivery, not a deployment recommendation.

## Before and after

- Before: ADC 4095 could become 0% and trigger the target allowance. Now: exact 12-bit rails (0/4095) and out-of-range samples are rejected before averaging. No inferred near-rail threshold or calibration changes.
- Before: ADC read/setup failure could leave an old percentage available. Now: explicit quality, null API percentage, averaging reset, and full-window recovery. Monotonic age over 300 seconds marks an otherwise valid reading stale, independently of clock corrections. Settings/probe changes invalidate mismatched readings.
- Before: missing/stale/warming probe invoked the target watering allowance. Now: daily target checks skip with `fault` history and diagnostics; invalid soak checks cancel further requests. A pulse already requested is still bounded by the existing pump timer. Timed schedules, manual control, and the allowance for a valid but low-response probe retain their behavior.
- Before: a disconnected page could retain its last percentage. Now: an explicit offline diagnostic replaces that metric; requests time out after eight seconds. Historical plots remain historical. Each API snapshot is copied under a short critical section.

Quality values: `disabled`, `warming`, `valid`, `saturated`, `read_error`, `invalid_config`, `stale`. Only `valid` exposes a percentage. Saturation describes an ADC observation, not a hardware diagnosis. In-range readings can still be physically wrong.

## Verification

Run `python test/run_host_moisture_tests.py` and the four existing `test/run_host_{target,pump,model,reservoir}_tests.py` scripts individually. All five passed on November 12 using host GCC/Node. The moisture test runs the actual task, snapshot getter, API serializer and UI rendering function with rail, read/setup failure, invalid calibration/GPIO, startup, stale/recovery, disabled and offline/reconnect fixtures. Embedded JavaScript parses. Scheduler fixtures cover every invalid quality before initial and soak requests, wet skips, valid pulses, daily limits and duplicate protection over 14 simulated days. Existing pump/reservoir/model checks pass.

Review covered intended diff, unchanged calibration and pump paths, publication file list and whitespace checks. Full ESP-IDF compile/size verification, real browser layout, deployed firmware identity, physical accuracy and device recovery remain unverified. No build, flash, OTA, reboot, live pump command or service restart was performed. Merged source must remain undeployed pending authorized firmware validation and physical comparison.

## Repository CI

`.github/workflows/host-tests.yml` runs all five host suites on Ubuntu for pull requests and pushes to `main` and the correction branch. Actions and the ESP-IDF v5.5.1 test dependency revision are pinned; only cJSON/Unity source submodules are fetched. No firmware toolchain, device commands or deployment steps run. CI verifies host behavior, not physical accuracy or firmware image size. PR checks and the post-merge push run must both pass at their exact revisions before delivery is complete.

## Physical comparison for Leo

1. With power off, isolate pump power so automatic commands cannot deliver water. Keep that isolation for this comparison. Photograph probe depth/location, connector orientation and wire labels before moving anything. Restore only controller/probe power when safe.
2. Read the configured probe GPIO, dry/wet raw values, averaging count and live interval through the existing Settings/API without saving changes. Use the controller's known address, not an address inferred from the other copy. Record firmware identity if available. Do not recalibrate.
3. Leave the probe in place. At its sensing depth, describe soil beside it as wet/damp/dry. Save five successive `GET /api/v1/moisture/latest` responses separated by the configured live interval: timestamp, raw ADC, percent, sample count and quality (quality exists only after an authorized deployment of this correction). Record whether the pump supply is isolated.
4. Using a DC multimeter on the low-voltage side only, measure probe VCC-to-probe-GND, AO-to-probe-GND, and AO-to-controller-GND alongside those readings. Record actual volts and the configured GPIO; the documented wiring is VCC 3.3 V, common GND, AO to GPIO1 by default. Stop for supply/pin mismatch; do not connect AO above the board's allowed input voltage. Do not probe mains or relay contacts.
5. Power off before reseating. Check continuity of GND and AO end-to-end and inspect loose/corroded joints. Reseat only the same labeled connections, restore controller/probe power with pump still isolated, and repeat steps 3–4 at unchanged depth. If available, compare a known-good identical probe in the same soil/depth with the same supply, recording its uncalibrated raw ADC and AO voltage; do not reuse the old percentage calibration as proof of accuracy.

Compare soil condition, raw ADC and measured AO together. Stable AO with changing ADC points toward the controller input/reference/ground path; AO changes with supply or connection changes point toward that path; a difference between probes under matched conditions needs further investigation. These are comparison clues, not conclusions. Preserve original readings and photographs. Do not resume unattended watering or choose new calibration from this comparison alone if the readings remain unexplained.
