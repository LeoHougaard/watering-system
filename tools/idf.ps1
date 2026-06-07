$ErrorActionPreference = 'Stop'

$idfPath = $env:IDF_PATH
if (-not $idfPath) {
    $idfPath = Join-Path $env:USERPROFILE 'esp\v5.5.1\esp-idf'
}

$exportScript = Join-Path $idfPath 'export.ps1'
if (-not (Test-Path $exportScript)) {
    throw "ESP-IDF export script not found at '$exportScript'. Set IDF_PATH or install ESP-IDF v5.5.1 under '$env:USERPROFILE\esp\v5.5.1\esp-idf'."
}

& $exportScript

$env:PATH = (($env:PATH -split ';') | Where-Object {
    $_ -and
    $_ -notlike 'C:\Strawberry\c\bin*' -and
    $_ -notlike 'C:\Strawberry\perl\bin*'
}) -join ';'

& idf.py @args
exit $LASTEXITCODE
