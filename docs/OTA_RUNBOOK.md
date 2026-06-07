# OTA Update Runbook

Use this when a future thread needs to build and upload firmware over Wi-Fi.

## Current Device

- Device URL: `http://10.1.40.177/`
- OTA endpoint: `http://10.1.40.177/api/v1/ota/apply`
- Local API PIN: `1234`
- Firmware image path: `build_s3\esp32_irrigation_controller.bin`

## Build

Use the fast no-space build path first:

```powershell
.\tools\build_s3_fast.cmd
```

This stages the project under `C:\Temp\WateringSystem`, reuses the temp build directory for incremental builds, and copies the final `.bin`, `.elf`, and `.map` back to `build_s3`. This avoids the Windows/ESP-IDF compiler short-path failure seen when building directly from `C:\Users\Leo\OneDrive\Projects\Watering system`.

From the project root, build with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_s3.ps1
```

`tools\build_s3.ps1` sources ESP-IDF from:

```text
C:\Users\Leo\esp\v5.5.1\esp-idf\export.ps1
```

It also removes Strawberry Perl paths from `PATH` for the build process. This avoids the wrong `ccache.exe` being selected and causing ESP-IDF toolchain failures.

If sandboxed command execution fails with a compiler panic like `Failed to get path name. Error code: 5`, use `tools\build_s3_fast.cmd` instead of debugging the normal path.

## Upload OTA

For the current controller, the quickest path is:

```powershell
.\tools\ota_s3_fast.cmd
```

Optional overrides:

```powershell
$env:WATERING_DEVICE_URL = "http://10.1.40.177"
$env:WATERING_LOCAL_PIN = "1234"
$env:WATERING_BUILD_STAGE = "C:\Temp\WateringSystem"
.\tools\ota_s3_fast.cmd
```

After a successful build, upload with `curl.exe`. Use HTTP/1.0 and explicit timeouts; this has been more reliable on the ESP32 HTTP server than the default curl connection behavior:

```powershell
curl.exe --http1.0 --no-buffer --connect-timeout 10 --max-time 180 `
  -X POST http://10.1.40.177/api/v1/ota/apply `
  -H "X-Local-PIN: 1234" `
  -H "Content-Type: application/octet-stream" `
  --data-binary "@build_s3\esp32_irrigation_controller.bin"
```

Expected response:

```json
{"ok":true,"rebooting":true}
```

`Invoke-WebRequest` threw a client-side null-reference exception during the last successful OTA session, so prefer `curl.exe` for binary upload.

## Verify After Reboot

Wait about 8 seconds, then query:

```powershell
curl.exe -s http://10.1.40.177/api/v1/status
curl.exe -s http://10.1.40.177/api/v1/settings
```

Healthy pump status after the reservoir-safety fix should include:

```json
{
  "pump_state": "IDLE",
  "reservoir_ok": true,
  "pump_gpio": 5,
  "pump_gpio_b": 6,
  "pump_gpio_level": 1,
  "pump_gpio_b_level": 0,
  "reservoir_bypass": true,
  "block_reason": ""
}
```

## Notes

- OTA preserves NVS settings. If a firmware default changes, the live device may still use older saved settings until `/api/v1/settings` is updated or NVS is erased.
- Pump GPIO settings are loaded by `pump_controller_start()` at boot. If GPIO settings are changed through `/api/v1/settings`, reboot the controller before expecting `/api/v1/status` to reflect the new pump pins.
- The current relay-module wiring uses GPIO5 for the active-low relay module IN1 control input. GPIO5 high means pump off; GPIO5 low means pump on. GPIO6 is still held low as the secondary pump output for compatibility with the previous DRV8833 wiring and can remain disconnected.
- Current OTA reliability hardening in firmware: station Wi-Fi power save is disabled, HTTP receive timeout is 120 seconds, HTTP server stack is 8192 bytes, OTA upload size is validated before writing, OTA writes use a 4096-byte buffer, and OTA failures log received byte counts.
