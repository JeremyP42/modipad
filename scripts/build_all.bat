@echo off
chcp 65001 >nul
title ModiPAD - Build firmware + storage
color 07
echo.
echo ========================================
echo   MODIPAD - BUILD FIRMWARE + STORAGE
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo [1/3] Cleaning old files...
"%PIO%" run -t clean >nul 2>&1

echo [2/3] Compiling firmware...
"%PIO%" run
if %errorlevel% neq 0 (
    echo.
    color 07
    echo X FIRMWARE BUILD FAILED
    pause
    exit /b
)

echo.
echo [3/3] Building LittleFS image...
"%PIO%" run -t buildfs
if %errorlevel% neq 0 (
    echo.
    color 07
    echo X STORAGE BUILD FAILED
    pause
    exit /b
)

echo.
color 07
echo ========================================
echo   OK BUILD SUCCEEDED (firmware + storage)
echo ========================================
echo   firmware.bin + littlefs.bin in .pio\build\modipad\
echo   archived under firmware\^<version^>\
echo.
pause
