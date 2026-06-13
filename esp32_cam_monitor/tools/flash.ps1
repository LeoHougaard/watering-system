$ErrorActionPreference = 'Stop'

param(
    [string]$Port = $env:ESP32_CAM_PORT
)

if (-not $Port) {
    throw "Pass -Port COMx or set ESP32_CAM_PORT."
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $projectRoot
try {
    & ..\tools\idf.cmd -p $Port flash monitor
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
