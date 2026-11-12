# Software checks

Run with GCC and the local ESP-IDF installation available:

- `python test/run_host_model_tests.py`: settings limits and model serialization.
- `python test/run_host_target_tests.py`: fourteen days of the actual scheduler, daily timing, wet-soil skips, missing probes, Stop and runtime limits.
- `python test/run_host_pump_tests.py`: actual pump task with simulated GPIO, queue handling, reservoir input and clocks. Includes clock corrections, cooldown, changed limits and queued-start cancellation.
- `python test/run_host_reservoir_tests.py`: debounce, polarity, bypass and reconfiguration.
- `python test/preview_server.py`: read-only UI preview. Evaluate both browser regression scripts there; all control requests are mocked and the preview rejects POST requests.

Build with `tools/build_s3_fast.cmd`. Compare the deployed HTML against the built source and read the live settings, status and history after deployment. Confirm the chosen target, time and cap are preserved, the clock is valid, the pump is idle and the stored records remain available.

Physical testing is not part of this update, at Leo's request.
