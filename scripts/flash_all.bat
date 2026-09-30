@echo off
chcp 65001 >nul
title ModiPAD - Flash ALL
color 07
echo.
echo ========================================
echo   MODIPAD - FLASH ALL
echo ========================================
echo   [1] firmware
echo   [2] internal storage (LittleFS)
echo   [3] SD card media
echo ========================================
echo.

cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo [1/3] Uploading firmware...
echo.
"%PIO%" run -t upload
if %errorlevel% neq 0 (
    echo.
    color 07
    echo X Firmware upload failed
    pause
    exit /b
)

echo.
echo [2/3] Uploading internal storage (LittleFS)...
echo.
"%PIO%" run -t uploadfs
if %errorlevel% neq 0 (
    echo.
    color 07
    echo X Filesystem upload failed
    pause
    exit /b
)

echo.
echo [3/3] Uploading SD card media...
echo.
call "%~dp0sync_sd.bat"

echo.
color 07
echo ========================================
echo   OK ALL DONE (firmware + storage + SD)
echo ========================================
echo.
pause
