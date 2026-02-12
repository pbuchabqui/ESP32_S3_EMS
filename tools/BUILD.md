# ESP32-EFI - Build Guide (ESP32-S3)

## Requirements

- ESP-IDF `v5.5.1` or newer
- Python `3.8+`
- CMake (via ESP-IDF)

## Quick build

```bash
tools/build_all.ps1
# or
tools/build_all.bat
```

## Manual build

```bash
cd firmware/s3
$env:IDF_PATH="C:\Espressif\frameworks\esp-idf-v5.5.1"
. $env:IDF_PATH\export.ps1
idf.py build
```

## Flash

```bash
cd firmware/s3
idf.py -p PORT flash
idf.py -p PORT monitor
```

## Artifacts

- ELF/BIN output: `firmware/s3/build`
- Main app binary: `firmware/s3/build/ecu_s3_firmware.bin`

## Target status

- Hardware target in production flow: **ESP32-S3**
- `firmware/s3` build validated on **2026-02-11**