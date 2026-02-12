@echo off
echo Building ECU ESP32-S3...
echo =======================

REM Wrapper for the unified build script
call "%~dp0build_all.bat"
exit /b %ERRORLEVEL%