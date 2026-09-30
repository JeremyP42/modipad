@echo off
chcp 65001 >nul
title ModiPAD - Generate Fonts
color 07
echo.
echo ========================================
echo   MODIPAD - GENERATE FONTS
echo ========================================
echo.

cd /d "%~dp0.."

where lv_font_conv >nul 2>&1
if errorlevel 1 set "PATH=%APPDATA%\npm;%PATH%"
where lv_font_conv >nul 2>&1
if errorlevel 1 (
    color 07
    echo X lv_font_conv not found.
    echo   Install it once with:  npm install -g lv_font_conv
    echo.
    pause
    exit /b
)

set "REG=tools\fonts\Roboto-Regular.ttf"
set "BOLD=tools\fonts\Roboto-Bold.ttf"

if not exist "%REG%" (
    color 07
    echo X Missing %REG%
    pause
    exit /b
)
if not exist "%BOLD%" (
    color 07
    echo X Missing %BOLD%
    pause
    exit /b
)
if not exist "datadevice\fonts" mkdir "datadevice\fonts"

set "RANGES=-r 0x20-0x7F -r 0x400-0x4FF"

for %%S in (10 12 14 16 18) do (
    echo [regular %%S] roboto_%%S.bin
    lv_font_conv --font "%REG%" --size %%S --bpp 4 --format bin --no-compress %RANGES% -o datadevice\fonts\roboto_%%S.bin
    echo [bold %%S] roboto_%%S_bold.bin
    lv_font_conv --font "%BOLD%" --size %%S --bpp 4 --format bin --no-compress %RANGES% -o datadevice\fonts\roboto_%%S_bold.bin
)

echo.
color 07
echo ========================================
echo   OK FONTS GENERATED (10/12/14/16/18 + bold)
echo ========================================
dir /b datadevice\fonts\*.bin
echo.
pause
