# ESP32-CAM Monitor

Standalone AI-Thinker ESP32-CAM firmware for the watering system camera node.

- Stream: `http://esp32-cam.local/stream`
- Snapshot: `http://esp32-cam.local/jpg`
- Status: `http://esp32-cam.local/status`
- OTA: `POST /ota/apply` with `X-Local-PIN: 1234`

Initial flash still requires USB serial. Future updates can use OTA.

Build:

```powershell
.\tools\build.cmd
```

Initial flash, after the USB-to-serial adapter appears as a real COM port:

```powershell
.\tools\flash.cmd -Port COMx
```

OTA after initial flash:

```powershell
.\tools\ota.cmd
```
