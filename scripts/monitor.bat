@echo off
chcp 65001 >nul
title ModiPAD - Serial Monitor
color 07
echo.
echo ========================================
echo   MODIPAD - SERIAL MONITOR
echo ========================================
echo.
echo Press Ctrl+C to exit
echo.

REM Go to the project root
cd /d "%~dp0.."

REM --- resolve PlatformIO (PATH or default penv) ---
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

"%PIO%" device monitor --baud 115200
