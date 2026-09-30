@echo off
chcp 65001 >nul
title ModiPAD - Build
color 07
echo.
echo ========================================
echo   MODIPAD - BUILD
echo ========================================
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

echo [1/2] Cleaning old files...
"%PIO%" run -t clean >nul 2>&1

echo [2/2] Compiling firmware...
echo.

"%PIO%" run

if "%errorlevel%" == "0" (
    echo.
    color 07
    echo ========================================
    echo   OK BUILD SUCCEEDED
    echo ========================================
) else (
    echo.
    color 07
    echo ========================================
    echo   X BUILD FAILED
    echo ========================================
)

echo.
pause
