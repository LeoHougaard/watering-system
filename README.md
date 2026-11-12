# ESP32-S3 Irrigation Controller

ESP-IDF firmware for a local-only irrigation controller for seven planter containers. One shared pump waters all containers together through manually adjusted drip emitters. Watering uses a moisture target checked at one chosen time every day, or a timed weekday schedule. Select the mode, percentage and daily time in Schedule.

Target board: Waveshare ESP32-S3-Zero / ESP32-S3-Zero-M with 4 MB flash and 2 MB PSRAM.

## Features

- Pump relay control through an active-low relay module with pump-off default after firmware startup.
- One manual duration selector, Run and Stop.
- Pump countdown, immediate stop, max runtime, cooldown, and event logging.
- Optional debounced reservoir-level input with active-high or active-low configuration.
- Optional capacitive soil-moisture sensor on an ESP32-S3 ADC1 GPIO.
- One optional target percentage and daily time: two-minute pulses followed by 30-minute soak checks, within the daily runtime limit. An invalid, stale or missing probe skips target watering. A valid probe with little measured response retains the bounded daily allowance. The percentage represents one pot and needs local calibration.
- Scheduled watering with daily default at 06:00 for 2 minutes, max 5 minutes.
- Automatic network time synchronization; schedules remain locked out until the clock is valid.
- Local web UI and `/api/v1/*` endpoints.
- NVS settings storage plus bounded SPIFFS logs. Hourly moisture history is kept
  for at least one year in monthly files, while graph responses are downsampled
  on the device to stay fast and memory-safe.
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

On first boot, the device prints a unique controller PIN and a temporary setup Wi-Fi name/password to USB serial. Save the controller PIN, connect to the setup network, open `http://192.168.4.1`, and submit home Wi-Fi credentials through `/api/v1/wifi`. The controller restarts and joins the saved network. No home-network password or universal control PIN is compiled into the firmware.

## Over-The-Air Updates

The first firmware upload must be done over USB serial. After that, build a new firmware image and upload `build_s3/esp32_irrigation_controller.bin` over Wi-Fi. In VS Code, run the task `ESP-IDF: OTA upload over Wi-Fi`, or use:

```powershell
Invoke-WebRequest -Method Post -Uri http://192.168.4.1/api/v1/ota/apply -Headers @{"X-Local-PIN"="<controller-pin>"} -InFile build_s3\esp32_irrigation_controller.bin -ContentType "application/octet-stream"
```

The controller writes the uploaded binary to the inactive OTA partition, marks it as the next boot image, sends an `{ "ok": true, "rebooting": true }` response, and restarts. The bootloader rolls back an image that cannot complete startup checks.

For the current deployed controller at `http://10.1.40.117/`, use the project runbook: [docs/OTA_RUNBOOK.md](docs/OTA_RUNBOOK.md).

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

## Capacitive Soil Moisture Sensor Wiring

Default moisture sensor pin:

- Sensor AOUT/AO -> ESP32-S3 GPIO 1
- Sensor GND -> ESP32 GND
- Sensor VCC -> 3.3 V

Use the sensor's analog output, usually labeled `AOUT` or `AO`, not the digital `DOUT` pin. GPIO1 is the default because it maps to ESP32-S3 ADC1 channel 0 and does not conflict with the default pump relay pins. Other supported ADC1 pins are GPIO1 through GPIO10, but avoid GPIO4 if you are using the reservoir sensor and avoid GPIO5/GPIO6 with the default pump wiring.

Calibration is under Settings > Calibration. The sensor averages five readings taken one minute apart and saves an hourly history point. Set Dry and Wet using measured raw values. Hardware pins, sampling and storage settings remain available through the authenticated settings API for maintenance.

The graph offers 24-hour, 7-day and 30-day views. The controller retains a year of hourly data in bounded monthly files. `GET /api/v1/moisture?days=365&max_points=800` exports older records. Existing data is preserved during upgrades.
