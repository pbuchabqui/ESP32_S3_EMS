Pasta de referencia do projeto (Windows): `C:\Users\Pedro\Desktop\ESP32-EFI`

# ESP32-EFI - Build Instructions (ESP32-S3)

## Prerequisites

1. **ESP-IDF v5.5.1+** installed and configured
2. **CMake** (included with ESP-IDF)
3. **Python 3.8+**

## Build

### Option 1: script (recommended)

```bash
tools/build_all.bat
# or
tools/build_all.ps1
```

### Option 2: manual

```bash
cd C:\Users\Pedro\Desktop\ESP32-EFI\firmware\s3
$env:IDF_PATH="C:\Espressif\frameworks\esp-idf-v5.5.1"
. $env:IDF_PATH\export.ps1
idf.py build
```

## Flash

```bash
cd C:\Users\Pedro\Desktop\ESP32-EFI\firmware\s3
idf.py -p PORT flash
idf.py -p PORT monitor
```

Replace `PORT` with your serial port (e.g. `COM3`).

## Project structure

```text
ESP32-EFI/
+-- firmware/s3/              # ESP32-S3 ECU firmware
¦   +-- main/
¦   +-- components/engine_control/
¦   +-- sdkconfig
¦   +-- CMakeLists.txt
+-- tools/
    +-- build_all.bat
    +-- build_all.ps1
```

## Notes

- Active hardware target: **ESP32-S3**
- Build of `firmware/s3` validated locally on **2026-02-11**