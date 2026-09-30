@echo off
chcp 65001 >nul
title ModiPAD - Web UI preview
color 07
REM Starts a local preview of the web configurator (works without the device).
REM Files are served from .\datadevice (config.json, images/) so edits are saved
REM back to datadevice\config.json - exactly like the on-device web server.
REM A copy is mirrored to datasdcard\modipad\config\backup_preview.json so it
REM can also be restored from the device (Settings -> config backup) after [5].
REM To put the edited config on the device: menu [4] Upload storage only.

cd /d "%~dp0.."

set "PORT=8765"
set "PYCMD=py -3"
where py >nul 2>&1 || set "PYCMD=python"

echo ========================================
echo   MODIPAD - WEB UI PREVIEW
echo ========================================
echo.
echo  Serving: %CD%\datadevice
echo  Saving:  %CD%\datadevice\config.json  (+ SD backup mirror)
echo  Opening: http://127.0.0.1:%PORT%/
echo.
echo  After editing: close this preview, then menu [4] Upload storage only
echo  to flash the new config to the device.
echo.

start "ModiPAD web preview" /min %PYCMD% "%CD%\tools\utils\web_preview_server.py" %PORT%

REM give the server a moment to start, then open the browser
timeout /t 2 /nobreak >nul
start "" "http://127.0.0.1:%PORT%/"

echo  Preview started. Close the minimized "ModiPAD web preview"
echo  window to stop the server.
echo.
pause
