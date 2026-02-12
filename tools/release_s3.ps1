$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "firmware\s3\build"

$required = @(
    "ecu_s3_firmware.bin",
    "ecu_s3_firmware.elf",
    "bootloader\bootloader.bin",
    "partition_table\partition-table.bin"
)

foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $buildDir $f))) {
        throw "Missing build artifact: $f. Run firmware/s3 build first."
    }
}

$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $root "release\s3\$ts"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Copy-Item (Join-Path $buildDir "ecu_s3_firmware.bin") $outDir
Copy-Item (Join-Path $buildDir "ecu_s3_firmware.elf") $outDir
Copy-Item (Join-Path $buildDir "bootloader\bootloader.bin") $outDir
Copy-Item (Join-Path $buildDir "partition_table\partition-table.bin") $outDir

if (Test-Path (Join-Path $buildDir "flash_args")) {
    Copy-Item (Join-Path $buildDir "flash_args") $outDir
}

@"
ESP32-S3 release package generated: $ts
Files:
- bootloader.bin
- partition-table.bin
- ecu_s3_firmware.bin
- ecu_s3_firmware.elf
"@ | Set-Content -NoNewline (Join-Path $outDir "MANIFEST.txt")

Write-Host "Release package created at: $outDir"