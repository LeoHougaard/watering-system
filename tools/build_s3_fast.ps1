$ErrorActionPreference = 'Stop'

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$stageRoot = $env:WATERING_BUILD_STAGE
if (-not $stageRoot) {
    $stageRoot = 'C:\Temp\WateringSystem'
}

function Sync-Dir($name) {
    $src = Join-Path $projectRoot $name
    $dst = Join-Path $stageRoot $name
    robocopy $src $dst /MIR /NFL /NDL /NJH /NJS /NC /NS /XD build build_s3 build_s3_w build_s3_oled build_w .idf_tools | Out-Null
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed while syncing $name with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Force $stageRoot | Out-Null

foreach ($file in @('CMakeLists.txt', 'partitions.csv', 'sdkconfig.defaults', 'sdkconfig.s3', 'README.md')) {
    Copy-Item -Force (Join-Path $projectRoot $file) (Join-Path $stageRoot $file)
}

Sync-Dir 'main'
Sync-Dir 'components'
Sync-Dir 'tools'

$sourceTools = Join-Path $projectRoot '.idf_tools'
$stageTools = Join-Path $stageRoot '.idf_tools'
if ((Test-Path $sourceTools) -and -not (Test-Path $stageTools)) {
    Write-Host "Copying ESP-IDF tools into $stageTools. This is a one-time setup for the fast build path."
    robocopy $sourceTools $stageTools /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed while copying .idf_tools with exit code $LASTEXITCODE"
    }
}

$env:IDF_TOOLS_PATH = $stageTools
Push-Location $stageRoot
try {
    & (Join-Path $stageRoot 'tools\idf.cmd') -B build_s3 -D "SDKCONFIG=sdkconfig.s3" -D "IDF_TARGET=esp32s3" build
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}

$stageBuild = Join-Path $stageRoot 'build_s3'
$projectBuild = Join-Path $projectRoot 'build_s3'
New-Item -ItemType Directory -Force $projectBuild | Out-Null
foreach ($artifact in @('esp32_irrigation_controller.bin', 'esp32_irrigation_controller.elf', 'esp32_irrigation_controller.map', 'project_description.json')) {
    $src = Join-Path $stageBuild $artifact
    if (Test-Path $src) {
        Copy-Item -Force $src (Join-Path $projectBuild $artifact)
    }
}

Write-Host "Fast build complete. Firmware: $projectBuild\esp32_irrigation_controller.bin"
