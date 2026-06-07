$ErrorActionPreference = 'Stop'

$deviceUrl = $env:WATERING_DEVICE_URL
if (-not $deviceUrl) {
    $deviceUrl = 'http://10.1.40.177'
}
$pin = $env:WATERING_LOCAL_PIN
if (-not $pin) {
    $pin = '1234'
}

& "$PSScriptRoot\build_s3_fast.ps1"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$firmware = Join-Path $projectRoot 'build_s3\esp32_irrigation_controller.bin'
if (-not (Test-Path $firmware)) {
    throw "Firmware image not found at $firmware"
}

Write-Host "Checking controller at $deviceUrl..."
& curl.exe --fail --silent --show-error --max-time 10 -H "X-Local-PIN: $pin" "$deviceUrl/api/v1/status"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Uploading OTA image..."
& curl.exe --fail --http1.0 --no-buffer --connect-timeout 10 --max-time 180 `
    -X POST "$deviceUrl/api/v1/ota/apply" `
    -H "X-Local-PIN: $pin" `
    -H "Content-Type: application/octet-stream" `
    --data-binary "@$firmware"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Waiting for reboot..."
Start-Sleep -Seconds 8
& curl.exe --fail --silent --show-error --max-time 15 -H "X-Local-PIN: $pin" "$deviceUrl/api/v1/status"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "OTA complete."
