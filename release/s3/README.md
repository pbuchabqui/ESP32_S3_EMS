# S3 Release Bundle

## Build

```powershell
$env:IDF_PATH="C:\Espressif\frameworks\esp-idf-v5.5.1"
. $env:IDF_PATH\export.ps1
cd firmware/s3
idf.py build
```

## Package

```powershell
.\tools\release_s3.ps1
```

Generated files are saved into `release/s3/<timestamp>/`.