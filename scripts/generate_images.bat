@echo off
chcp 65001 >nul
title ModiPAD - Generate Images
color 07
echo.
echo ========================================
echo   MODIPAD - GENERATE IMAGES
echo ========================================
echo.

cd /d "%~dp0.."

REM === Detect best available compressor ===
set "COMPRESSOR="
set "COMPRESSOR_NAME="
set "COMPRESSOR_ARGS="

if exist "tools\oxipng\oxipng.exe" (
    set "COMPRESSOR=tools\oxipng\oxipng.exe"
    set "COMPRESSOR_NAME=Oxipng"
    set "COMPRESSOR_ARGS=-o 4 --strip safe"
    goto :found_compressor
)
if exist "tools\ect\ect.exe" (
    set "COMPRESSOR=tools\ect\ect.exe"
    set "COMPRESSOR_NAME=ECT"
    set "COMPRESSOR_ARGS=-9 -strip"
    goto :found_compressor
)
if exist "tools\optipng\optipng64.exe" (
    set "COMPRESSOR=tools\optipng\optipng64.exe"
    set "COMPRESSOR_NAME=OptiPNG 64-bit"
    set "COMPRESSOR_ARGS=-o5 -quiet"
    goto :found_compressor
)
if exist "tools\optipng\optipng32.exe" (
    set "COMPRESSOR=tools\optipng\optipng32.exe"
    set "COMPRESSOR_NAME=OptiPNG 32-bit"
    set "COMPRESSOR_ARGS=-o5 -quiet"
    goto :found_compressor
)
where oxipng >nul 2>&1
if "%errorlevel%" == "0" (
    set "COMPRESSOR=oxipng"
    set "COMPRESSOR_NAME=Oxipng (system)"
    set "COMPRESSOR_ARGS=-o 4 --strip safe"
    goto :found_compressor
)
where optipng >nul 2>&1
if "%errorlevel%" == "0" (
    set "COMPRESSOR=optipng"
    set "COMPRESSOR_NAME=OptiPNG (system)"
    set "COMPRESSOR_ARGS=-o5 -quiet"
    goto :found_compressor
)

echo ! No compressor found - optimization will be skipped
echo   Recommended: put oxipng.exe into tools\oxipng\
echo   Download: https://github.com/shssoichiro/oxipng/releases

:found_compressor
if defined COMPRESSOR echo Compressor: %COMPRESSOR_NAME%
echo.

REM === Step 1: generate PNG ===
echo [1/2] Generating images...
echo.

if not exist "tools\utils\generate_assets.ps1" (
    color 07
    echo X tools\utils\generate_assets.ps1 not found
    pause
    exit /b
)

powershell -ExecutionPolicy Bypass -File "tools\utils\generate_assets.ps1"
if %errorlevel% neq 0 (
    color 07
    echo X Image generation failed
    pause
    exit /b
)

echo.

REM === Step 2: optimize PNG ===
if not defined COMPRESSOR (
    echo [2/2] Optimization skipped ^(no compressor^)
    color 07
    echo.
    echo OK Images generated!
    echo.
    pause
    exit /b
)

echo [2/2] Optimizing PNG with %COMPRESSOR_NAME%...
echo.

set "COUNT=0"
set "TOTAL_BEFORE=0"
set "TOTAL_AFTER=0"

for /r "datadevice\images" %%f in (*.png) do (
    for %%A in ("%%f") do set /a "SIZE_BEFORE=%%~zA"
    "%COMPRESSOR%" %COMPRESSOR_ARGS% "%%f" >nul 2>&1
    for %%A in ("%%f") do set /a "SIZE_AFTER=%%~zA"
    set /a "TOTAL_BEFORE+=SIZE_BEFORE"
    set /a "TOTAL_AFTER+=SIZE_AFTER"
    set /a "COUNT+=1"
)

set /a "TOTAL_SAVED=TOTAL_BEFORE-TOTAL_AFTER"
if %TOTAL_BEFORE% GTR 0 (
    set /a "PERCENT=TOTAL_SAVED*100/TOTAL_BEFORE"
) else (
    set /a "PERCENT=0"
)

echo.
color 07
echo ========================================
echo   OK OPTIMIZATION COMPLETED (%COMPRESSOR_NAME%)
echo ========================================
echo   Files processed: %COUNT%
echo   Before: %TOTAL_BEFORE% bytes
echo   After:  %TOTAL_AFTER% bytes
echo   Saved:  %TOTAL_SAVED% bytes (%PERCENT%%%)
echo ========================================
echo.
pause
