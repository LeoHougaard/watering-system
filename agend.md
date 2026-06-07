# Agent Notes

## Fast Path

- Build: `.\tools\build_s3_fast.cmd`
- Build and OTA: `.\tools\ota_s3_fast.cmd`
- Device URL: `http://10.1.40.177`
- Local PIN: `1234`
- Firmware artifact: `build_s3\esp32_irrigation_controller.bin`

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

Last known good image size: `0xec2a0`, leaving `0x13d60` free.

## Publish Hygiene

Do not commit build output or local ESP-IDF tools:

- `.idf_tools/`
- `build*/`
- `C:\Temp\WateringSystem`
