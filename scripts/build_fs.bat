@echo off
chcp 65001 >nul
title ModiPAD - Build storage (LittleFS)
color 07
echo.
echo ========================================
echo   MODIPAD - BUILD INTERNAL STORAGE
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo Building LittleFS image from datadevice\ ...
echo.

"%PIO%" run -t buildfs

if "%errorlevel%" == "0" (
    echo.
    color 07
    echo ========================================
    echo   OK STORAGE BUILT
    echo ========================================
    echo   Image:  .pio\build\modipad\littlefs.bin
    echo   Copy:   firmware\^<version^>\littlefs_^<version^>_^<stamp^>.bin
    echo           placed next to the archived firmware.bin
) else (
    echo.
    color 07
    echo ========================================
    echo   X STORAGE BUILD FAILED
    echo ========================================
)

echo.
pause
