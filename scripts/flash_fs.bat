@echo off
chcp 65001 >nul
title ModiPAD - Flash storage (LittleFS)
color 07
echo.
echo ========================================
echo   MODIPAD - FLASH INTERNAL STORAGE
echo ========================================
echo.
echo Updates only the internal LittleFS
echo (config.json, images, web UI, fonts) -
echo no firmware reflash. Reboot to apply.
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
    echo OK Storage uploaded
    echo.
    echo Reboot the device to apply the changes
) else (
    echo.
    color 07
    echo X Upload failed
)

echo.
pause
