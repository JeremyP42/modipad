@echo off
chcp 65001 >nul
title ModiPAD - Upload Files Only
color 07
echo.
echo ========================================
echo   MODIPAD - UPLOAD FILES ONLY
echo ========================================
echo.
echo Use this to update only images/configs
echo WITHOUT reflashing the firmware.
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo Uploading filesystem (LittleFS)...
echo.

"%PIO%" run -t uploadfs

if "%errorlevel%" == "0" (
    echo.
    color 07
    echo OK Files uploaded
    echo.
    echo Reboot the device to apply the changes
) else (
    echo.
    color 07
    echo X Upload failed
)

echo.
pause
