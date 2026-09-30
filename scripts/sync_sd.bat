@echo off
chcp 65001 >nul
title ModiPAD - Copy media to SD card
color 07
REM Copies datasdcard\modipad to the SD card (folder \modipad), so the device
REM media (backgrounds / images / fonts / sounds) is deployed without reflashing.
REM
REM The card is found automatically by its volume label (SDLABEL below). If it
REM is not found, the script asks for the drive letter and offers to set the
REM label so future runs are automatic.
cd /d "%~dp0.."

set "SDLABEL=MODIPAD"
set "DRIVE="

for /f "usebackq delims=" %%D in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0find_sd.ps1" "%SDLABEL%"`) do set "DRIVE=%%D"

if not "%DRIVE%"=="" (
    echo Found SD card "%SDLABEL%" at %DRIVE%\
    goto haveDrive
)

echo SD card labeled "%SDLABEL%" was not found.
echo   Tip: rename the card to %SDLABEL% once (right-click the drive -^> Rename),
echo        and this script will select it automatically from now on.
echo.
set /p DRIVE="Enter SD card drive letter (e.g. D): "
if "%DRIVE%"=="" (
    echo Cancelled.
    pause
    exit /b
)
set "DRIVE=%DRIVE:~0,1%:"

if not exist "%DRIVE%\" (
    color 07
    echo X Drive %DRIVE% was not found.
    pause
    exit /b
)

choice /c YN /n /m "Set this card's label to %SDLABEL% for automatic detection next time? [Y/N] "
if errorlevel 2 goto haveDrive
label %DRIVE% %SDLABEL% >nul 2>&1

:haveDrive
set "DEST=%DRIVE%\modipad"
echo SD card: %DRIVE%   target: %DEST%

if not exist "%DRIVE%\" (
    echo X Drive %DRIVE% was not found.
    pause
    exit /b
)
if not exist "%DEST%" mkdir "%DEST%" >nul 2>&1

for /f %%N in ('dir /s /b /a-d "datasdcard\modipad" 2^>nul ^| find /c /v ""') do set "FILECOUNT=%%N"
echo Copying datasdcard\modipad  -^>  %DEST%   (%FILECOUNT% files)
echo.
xcopy /E /Y /I "datasdcard\modipad" "%DEST%"

if %errorlevel% neq 0 (
    color 07
    echo X Copy failed. Check the drive letter and that the card is inserted.
) else (
    color 07
    echo OK Media copied to %DEST%
)
echo.
pause
