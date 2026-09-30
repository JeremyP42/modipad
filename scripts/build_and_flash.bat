@echo off
chcp 65001 >nul
title ModiPAD - Full Build + Flash
color 07
echo.
echo ========================================
echo   MODIPAD - FULL CYCLE
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo [1/4] Cleaning...
"%PIO%" run -t clean >nul 2>&1

echo [2/4] Building firmware...
"%PIO%" run
if %errorlevel% neq 0 (
    color 07
    echo X Build failed!
    pause
    exit /b
)

echo.
echo [3/4] Uploading firmware...
"%PIO%" run -t upload
if %errorlevel% neq 0 (
    color 07
    echo X Firmware upload failed!
    pause
    exit /b
)

echo.
echo [4/4] Uploading filesystem (LittleFS)...
"%PIO%" run -t uploadfs
if %errorlevel% neq 0 (
    color 07
    echo X Filesystem upload failed!
    pause
    exit /b
)

echo.
color 07
echo ========================================
echo   OK ALL STEPS COMPLETED
echo ========================================
echo.

set /p CHOICE="Start Serial Monitor? (Y/N): "
if /i "%CHOICE%"=="Y" (
    cls
    call "%~dp0monitor.bat"
)

pause
