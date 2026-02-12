@echo off
echo Building ECU ESP32-S3 Firmware
echo ===============================

if "%IDF_PATH%"=="" (
    echo ERROR: IDF_PATH is not set.
    echo Please run export.bat first or set IDF_PATH to your ESP-IDF path.
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

echo.
echo Building ESP32-S3 firmware...
pushd firmware/s3
call idf.py build
if %errorlevel% neq 0 (
    echo ERROR: Failed to build S3 firmware
    popd
    pause
    exit /b 1
)
popd

echo.
echo Build completed successfully!
echo.
echo Flash command:
echo   S3: cd firmware/s3 ^&^& idf.py -p PORT flash
echo.
pause