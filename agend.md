# Agent Notes

## Fast Path

- Build: `.\tools\build_s3_fast.cmd`
- Build and OTA: `.\tools\ota_s3_fast.cmd`
- Device URL: `http://10.1.40.177`
- Local PIN: `1234`
- Firmware artifact: `build_s3\esp32_irrigation_controller.bin`
- USB recovery port, when plugged in: usually `COM3`

## Why Fast Build Exists

The project path has a space: `Watering system`. ESP-IDF on Windows can fail with `Failed to get path name. Error code: 5`.

Use the fast scripts. They stage source under `C:\Temp\WateringSystem`, build there, then copy artifacts back.

## Hardware

- Board: Waveshare ESP32-S3-Zero / ESP32-S3-Zero-M
- Pump relay: GPIO 5 active-low, GPIO 6 held low and usually disconnected
- OLED: SSD1306 128x64 I2C at `0x3C`
- OLED SCL: GPIO 13
- OLED SDA: GPIO 12

## Size Constraint

OTA app partitions are 1 MB. Keep `sdkconfig.s3` optimized for size:

- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
- `CONFIG_COMPILER_OPTIMIZATION_LEVEL_RELEASE=y`

Last known good image size: `0xecc80`, leaving `0x13380` free.

## Recovery And OTA

- If OTA upload hangs or the controller disappears, do not keep retrying OTA blindly. Check `http://10.1.40.177/api/v1/status`, then `http://192.168.4.1/api/v1/status`.
- Do not probe arbitrary LAN neighbors with `X-Local-PIN`; that leaks the PIN to unknown devices.
- If USB is available, recover with direct `esptool` rather than the IDF wrapper:
  `.\.idf_tools\python_env\idf5.5_py3.14_env\Scripts\python.exe -m esptool --chip esp32s3 -p COM3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 build_s3\bootloader\bootloader.bin 0x8000 build_s3\partition_table\partition-table.bin 0xf000 build_s3\ota_data_initial.bin 0x20000 build_s3\esp32_irrigation_controller.bin`
- If `idf.py flash` times out, stop hung `idf.py`/`python`/`cmd` flash processes before retrying.

## Scheduling And Time

- The ESP32 clock starts near Unix epoch after reboot. Scheduling is wrong until time is set.
- Current fix: `/api/v1/time` accepts browser epoch time, and the UI syncs time before schedule apply.
- ESP/newlib does not understand `America/Vancouver` as `TZ`; map it to `PST8PDT,M3.2.0,M11.1.0`.
- Avoid starting SNTP before Wi-Fi is connected; browser time sync is safer for this local-only controller.
- On status/settings save, call `scheduler_refresh_next_run()` so stale `next_scheduled` text is not shown.
- Avoid using DOM ids like `console`; they collide with browser globals and can break page rendering.

## Publish Hygiene

Do not commit build output or local ESP-IDF tools:

- `.idf_tools/`
- `build*/`
- `C:\Temp\WateringSystem`
