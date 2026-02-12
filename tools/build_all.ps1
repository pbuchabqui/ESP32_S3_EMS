param(
    [switch]$Sim
)

$ErrorActionPreference = "Stop"

Write-Host "Building ECU ESP32-S3 Firmware"
Write-Host "==============================="

if (-not $env:IDF_PATH) {
    throw "IDF_PATH is not set. Set it to your ESP-IDF path (e.g. C:\Espressif\frameworks\esp-idf-v5.5.1) and re-run."
}

$exportPath = Join-Path $env:IDF_PATH "export.ps1"
if (-not (Test-Path $exportPath)) {
    throw "export.ps1 not found at $exportPath"
}

. $exportPath

Write-Host ""
Write-Host "Building ESP32-S3 firmware..."
Push-Location "firmware/s3"
try {
    idf.py build
} finally {
    Pop-Location
}

if ($Sim) {
    Write-Host ""
    Write-Host "Building virtual simulation target (tools/s3_jitter_sim)..."
    Push-Location "tools/s3_jitter_sim"
    try {
        idf.py build
    } finally {
        Pop-Location
    }
}

Write-Host ""
Write-Host "Build completed successfully!"
Write-Host ""
Write-Host "Flash command:"
Write-Host "  S3: cd firmware/s3 && idf.py -p PORT flash"
if ($Sim) {
    Write-Host "  SIM: cd tools/s3_jitter_sim && idf.py -p PORT flash monitor"
}
