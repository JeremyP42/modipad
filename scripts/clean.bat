@echo off
chcp 65001 >nul
title ModiPAD - Clean
color 07
echo.
echo ========================================
echo   MODIPAD - CLEAN
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

set /p CONFIRM="This deletes all build output. Are you sure? (Y/N): "
if /i not "%CONFIRM%"=="Y" (
    echo Cancelled.
    pause
    exit /b
)

echo.
echo Cleaning build directory...
"%PIO%" run -t clean

if exist .pio (
    rmdir /s /q .pio
    echo Removed .pio directory
)

echo.
color 07
echo OK Clean completed
echo.
pause
