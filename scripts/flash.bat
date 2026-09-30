@echo off
chcp 65001 >nul
title ModiPAD - Flash
color 07
echo.
echo ========================================
echo   MODIPAD - DEVICE FLASH
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo [1/3] Detecting serial ports...
for /f "usebackq tokens=*" %%p in (`powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames() -join ', '"`) do set "COM_PORTS=%%p"
if defined COM_PORTS (
    echo    Ports: %COM_PORTS%
) else (
    echo    ! No serial ports found - PlatformIO will try to auto-detect.
)
echo.

echo [2/3] Uploading firmware...
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
echo [3/3] Uploading filesystem (LittleFS)...
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
color 07
echo ========================================
echo   OK FLASH COMPLETED
echo ========================================
echo.
echo Device is ready to use!
echo.
pause
