@echo off
chcp 65001 >nul
title ModiPAD - Flash firmware only
color 07
echo.
echo ========================================
echo   MODIPAD - FLASH FIRMWARE ONLY
echo ========================================
echo.

cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo Uploading firmware only (no filesystem)...
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
color 07
echo ========================================
echo   OK FIRMWARE FLASHED
echo ========================================
echo.
pause
