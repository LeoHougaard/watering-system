# ESP32-S3 Irrigation Controller

ESP-IDF firmware for a local-only irrigation controller for seven planter containers. One shared pump waters all containers together through manually adjusted drip emitters. This iteration is schedule-based and does **not** measure soil moisture.

Target board: Waveshare ESP32-S3-Zero / ESP32-S3-Zero-M with 4 MB flash and 2 MB PSRAM.

## Features

- Pump relay control through an active-low relay module with pump-off default after firmware startup.
- Manual runs: 5 min, 10 min, 15 min, 20 min.
- Pump countdown, immediate stop, max runtime, cooldown, and event logging.
- Optional debounced reservoir-level input with active-high or active-low configuration.
- Local 1-inch SSD1306 I2C OLED status display for pump, reservoir, moisture, and next-run status.
- Scheduled watering with daily default at 06:00 for 2 minutes, max 5 minutes.
- Internet weather compensation using Open-Meteo forecast data, FAO-56 reference evapotranspiration, and forecast/recent rain.
- Seven editable planter profiles and user observation history.
- Plain-language recommendations based on schedules, history, plant profiles, and user observations.
- Local web UI and `/api/v1/*` endpoints.
- NVS settings storage plus SPIFFS append logs.
- OTA-ready partition table.

## Build

From VS Code, install the recommended **Espressif IDF** extension, run `ESP-IDF: Configure ESP-IDF Extension` once, then use **Terminal > Run Task**:

- `ESP-IDF: Set target esp32s3`
- `ESP-IDF: Build`
- `ESP-IDF: Flash over USB`
- `ESP-IDF: Flash and monitor over USB`
- `ESP-IDF: OTA upload over Wi-Fi`

For the first upload, connect the ESP32-S3-Zero over USB-C and enter the board's COM port when VS Code asks, for example `COM3`.

The same commands can be run from an ESP-IDF terminal:

```powershell
idf.py set-target esp32s3
idf.py -B build_s3 -D "SDKCONFIG=sdkconfig.s3" -D "IDF_TARGET=esp32s3" build
idf.py -B build_s3 -D "SDKCONFIG=sdkconfig.s3" -D "IDF_TARGET=esp32s3" flash monitor
```

From a normal terminal that does not already have `idf.py` on `PATH`, use the repo-local wrapper. It loads the ESP-IDF export script from `IDF_PATH`, or from `%USERPROFILE%\esp\v5.5.1\esp-idf` by default:

```powershell
.\tools\idf.cmd -B build_s3 -D "SDKCONFIG=sdkconfig.s3" -D "IDF_TARGET=esp32s3" build
.\tools\idf.cmd -B build_s3 -D "SDKCONFIG=sdkconfig.s3" -D "IDF_TARGET=esp32s3" flash monitor
```

For the common ESP32-S3 build, this shortcut runs the same wrapped command:

```powershell
.\tools\build_s3.cmd
```

On Windows, this project path contains a space and ESP-IDF may hit a compiler short-path bug. Prefer the fast staged build for day-to-day changes:

```powershell
.\tools\build_s3_fast.cmd
```

It builds from `C:\Temp\WateringSystem`, reuses incremental output there, and copies firmware artifacts back to `build_s3`.

On first boot, the device starts a setup Wi-Fi network named `IrrigationController` with password `water1234`. Connect to it, open `http://192.168.4.1`, then use the Wi-Fi tab to save your home Wi-Fi name and password. The controller restarts and joins your home network. The local UI write PIN is `1234` or `water1234` in this scaffold; replace this with a configured PIN hash before unattended deployment.

## Over-The-Air Updates

The first firmware upload must be done over USB serial. After that, build a new firmware image and upload `build_s3/esp32_irrigation_controller.bin` over Wi-Fi. In VS Code, run the task `ESP-IDF: OTA upload over Wi-Fi`, or use:

```powershell
Invoke-WebRequest -Method Post -Uri http://192.168.4.1/api/v1/ota/apply -Headers @{"X-Local-PIN"="1234"} -InFile build_s3\esp32_irrigation_controller.bin -ContentType "application/octet-stream"
```

The controller writes the uploaded binary to the inactive OTA partition, marks it as the next boot image, sends an `{ "ok": true, "rebooting": true }` response, and restarts.

For the current deployed controller at `http://10.1.40.177/`, use the project runbook: [docs/OTA_RUNBOOK.md](docs/OTA_RUNBOOK.md).

Fast build plus OTA:

```powershell
.\tools\ota_s3_fast.cmd
```

## Safety Notes

Use a relay module and enclosure rated for the pump and environment. Keep mains wiring physically isolated from ESP32 low-voltage wiring. The firmware drives the active-low pump relay GPIO high for off after startup, but electrical fail-safe design still matters.

## Relay Module Wiring

Default pump relay pin:

- ESP32-S3 GPIO 5 -> relay module IN1
- ESP32 GND -> relay module GND
- Relay module VCC -> the supply voltage required by the relay module

Pump on drives GPIO5 low. Pump off drives GPIO5 high. GPIO6 remains configured as the secondary pump output and is held low for compatibility with the previous DRV8833 wiring, but it does not need to be connected for a standard relay module. Change `pump_gpio` through the settings API if you wire the relay input to a different ESP32 pin.

If using a bare relay coil instead of a relay module, do not connect the coil directly to the ESP32. Use an appropriate relay driver or motor driver, and update the wiring/settings to match.

## OLED Status Display Wiring

The firmware drives a common inexpensive 1-inch 128x64 SSD1306 I2C OLED at address `0x3C`.

- OLED SCL -> ESP32-S3 GPIO 13
- OLED SDA -> ESP32-S3 GPIO 12
- OLED GND -> ESP32 GND
- OLED VCC -> 3.3 V

The display refreshes every 2 seconds and shows pump state, remaining run time, reservoir status, moisture status, and the next scheduled watering text. If the OLED is disconnected or not found at boot, the controller logs a warning and continues running normally.
