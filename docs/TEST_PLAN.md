# Test Plan

## Unit Tests

- Pump state transitions: `IDLE`, `MANUAL_RUNNING`, `SCHEDULED_RUNNING`, `COOLDOWN`, low-reservoir lockout, max-runtime lockout, fault.
- Manual timer expiration: requested run stops at requested duration or at 1200 seconds, whichever is lower.
- Reservoir lockout: low reservoir blocks manual and scheduled starts.
- Cooldown logic: run requests during cooldown are rejected or skipped without relay activation.
- Schedule calculation: enabled days and start minute trigger exactly once per day.
- Rain delay behavior: scheduled run is skipped until `rain_delay_until`.
- Seasonal multiplier: scheduled duration is multiplied then capped at 300 seconds.
- Recommendation rules: repeated dry/wet observations, stale observation reminder, manual-after-schedule pattern.
- Settings persistence: NVS defaults are created and saved settings survive reboot.

## Integration Tests

- 10-minute manual run starts and stops correctly.
- Manual stop works immediately.
- Low reservoir blocks manual run.
- Low reservoir blocks scheduled run.
- Low reservoir during run stops pump.
- Scheduled watering runs only once per scheduled window.
- Rain delay skips scheduled watering.
- Reboot preserves settings and planter profiles.
- Repeated dry observations create dripper-open recommendation.
- Repeated wet observations create dripper-close recommendation.
- Many dry observations create schedule-increase recommendation.
- Many wet observations create schedule-decrease recommendation.

## Hardware Safety Checks

- Confirm relay GPIO is low/off during boot and reset.
- Confirm relay module is wired fail-off for ESP32 reset and power loss.
- Confirm mains wiring is isolated in a rated enclosure.
- Confirm reservoir sensor polarity matches `reservoir_active_high`.
