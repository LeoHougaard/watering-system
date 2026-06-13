$ErrorActionPreference = 'Stop'

param(
    [string]$DeviceUrl = $env:ESP32_CAM_URL,
    [string]$Pin = $env:WATERING_LOCAL_PIN
)

if (-not $DeviceUrl) {
    $DeviceUrl = 'http://esp32-cam.local'
}
if (-not $Pin) {
    $Pin = '1234'
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$firmware = Join-Path $projectRoot 'build\esp32_cam_monitor.bin'
if (-not (Test-Path $firmware)) {
    Push-Location $projectRoot
    try {
        & ..\tools\idf.cmd build
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
}

& curl.exe --fail --silent --show-error --max-time 10 "$DeviceUrl/status"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& curl.exe --fail --http1.0 --no-buffer --connect-timeout 10 --max-time 180 `
    -X POST "$DeviceUrl/ota/apply" `
    -H "X-Local-PIN: $Pin" `
    -H "Content-Type: application/octet-stream" `
    --data-binary "@$firmware"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Start-Sleep -Seconds 8
& curl.exe --fail --silent --show-error --max-time 10 "$DeviceUrl/status"
