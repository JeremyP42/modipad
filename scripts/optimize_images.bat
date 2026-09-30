@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title ModiPAD - Optimize Images (Lossless)
color 07
echo.
echo ========================================
echo   MODIPAD - OPTIMIZE PNG
echo ========================================
echo.
echo Mode: Lossless (no quality loss)
echo Tools: Oxipng -^> ECT -^> OptiPNG
echo.

cd /d "%~dp0.."

REM === Detect best available compressor ===
set "COMPRESSOR="
set "COMPRESSOR_NAME="
set "COMPRESSOR_ARGS="

if exist "tools\oxipng\oxipng.exe" (
    set "COMPRESSOR=tools\oxipng\oxipng.exe"
    set "COMPRESSOR_NAME=Oxipng"
    set "COMPRESSOR_ARGS=-o 6 --strip safe"
    goto :found
)
if exist "tools\ect\ect.exe" (
    set "COMPRESSOR=tools\ect\ect.exe"
    set "COMPRESSOR_NAME=ECT"
    set "COMPRESSOR_ARGS=-9 -strip"
    goto :found
)
if exist "tools\optipng\optipng64.exe" (
    set "COMPRESSOR=tools\optipng\optipng64.exe"
    set "COMPRESSOR_NAME=OptiPNG 64-bit"
    set "COMPRESSOR_ARGS=-o7 -quiet"
    goto :found
)
if exist "tools\optipng\optipng32.exe" (
    set "COMPRESSOR=tools\optipng\optipng32.exe"
    set "COMPRESSOR_NAME=OptiPNG 32-bit"
    set "COMPRESSOR_ARGS=-o7 -quiet"
    goto :found
)
where oxipng >nul 2>&1
if "%errorlevel%" == "0" (
    set "COMPRESSOR=oxipng"
    set "COMPRESSOR_NAME=Oxipng (system)"
    set "COMPRESSOR_ARGS=-o 6 --strip safe"
    goto :found
)
where optipng >nul 2>&1
if "%errorlevel%" == "0" (
    set "COMPRESSOR=optipng"
    set "COMPRESSOR_NAME=OptiPNG (system)"
    set "COMPRESSOR_ARGS=-o7 -quiet"
    goto :found
)

color 07
echo X No compressor found!
echo.
echo Install one of:
echo   1. Oxipng (recommended): https://github.com/shssoichiro/oxipng/releases
echo   2. ECT:                  https://github.com/fhanau/Efficient-Compression-Tool/releases
echo   3. OptiPNG:              https://optipng.sourceforge.net/
echo.
echo Put the executable under tools\^<name^>\
echo.
pause
exit /b

:found
echo Using: %COMPRESSOR_NAME%
echo Args:  %COMPRESSOR_ARGS%
echo.

set "COUNT=0"
set "TOTAL_BEFORE=0"
set "TOTAL_AFTER=0"

echo Optimizing files...
echo.

for /r "datadevice\images" %%f in (*.png) do (
    for %%A in ("%%f") do set /a "SIZE_BEFORE=%%~zA"
    "%COMPRESSOR%" %COMPRESSOR_ARGS% "%%f" >nul 2>&1
    for %%A in ("%%f") do set /a "SIZE_AFTER=%%~zA"
    set /a "TOTAL_BEFORE+=SIZE_BEFORE"
    set /a "TOTAL_AFTER+=SIZE_AFTER"
    set /a "COUNT+=1"

    set /a "PROGRESS=COUNT %% 20"
    if !PROGRESS! == 0 echo   Processed: !COUNT! files
)

set /a "TOTAL_SAVED=TOTAL_BEFORE-TOTAL_AFTER"
if !TOTAL_BEFORE! GTR 0 (
    set /a "PERCENT=TOTAL_SAVED*100/TOTAL_BEFORE"
) else (
    set /a "PERCENT=0"
)

echo.
color 07
echo ========================================
echo   OK OPTIMIZATION COMPLETED (%COMPRESSOR_NAME%)
echo ========================================
echo   Files processed: !COUNT!
echo   Before: !TOTAL_BEFORE! bytes
echo   After:  !TOTAL_AFTER! bytes
echo   Saved:  !TOTAL_SAVED! bytes (!PERCENT!%%)
echo ========================================
echo.
pause
