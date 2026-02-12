@echo off
echo Configuring ECU ESP32-S3...
echo ==========================

if "%IDF_PATH%"=="" (
    echo ERROR: IDF_PATH is not set.
    echo Example:
    echo   set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.1
    echo   call %%IDF_PATH%%\export.bat
    exit /b 1
)

if exist "%IDF_PATH%\export.bat" (
    call "%IDF_PATH%\export.bat"
) else (
    echo ERROR: export.bat not found at %IDF_PATH%\export.bat
    exit /b 1
)

cd /d "%~dp0..\firmware\s3"

echo Opening menuconfig for ESP32-S3...
idf.py menuconfig

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Configuration completed successfully.
    echo To build: tools\build.bat
) else (
    echo.
    echo Configuration failed. Check messages above.
)

pause