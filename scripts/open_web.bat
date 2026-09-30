@echo off
chcp 65001 >nul
title ModiPAD - Web Interface

echo.
echo ========================================
echo   MODIPAD - WEB INTERFACE
echo ========================================
echo.

REM Wait a moment and open the web UI in the browser
timeout /t 2 /nobreak >nul

start http://192.168.4.1

echo OK Web interface opened in the browser
echo.
echo If the page did not open:
echo 1. Connect to WiFi "ModiPAD_Setup" (password: 12345678)
echo 2. Open http://192.168.4.1 in the browser
echo.
pause
