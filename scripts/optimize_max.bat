@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title ModiPAD - Maximum Optimization
color 07
echo.
echo ========================================
echo   MODIPAD - MAXIMUM OPTIMIZATION
echo ========================================
echo.
echo Mode: Oxipng (lossless) + Pngquant (lossy)
echo WARNING: pngquant is LOSSY - quality is slightly reduced.
echo.

cd /d "%~dp0.."

REM === Locate Oxipng ===
set "OXIPNG="
if exist "tools\oxipng\oxipng.exe" set "OXIPNG=tools\oxipng\oxipng.exe"
if not defined OXIPNG (
    where oxipng >nul 2>&1
    if !errorlevel! == 0 set "OXIPNG=oxipng"
)

REM === Locate Pngquant ===
set "PNGQUANT="
if exist "tools\pngquant\pngquant.exe" set "PNGQUANT=tools\pngquant\pngquant.exe"
if not defined PNGQUANT (
    where pngquant >nul 2>&1
    if !errorlevel! == 0 set "PNGQUANT=pngquant"
)

if not defined OXIPNG (
    color 07
    echo X Oxipng not found!
    echo   Download: https://github.com/shssoichiro/oxipng/releases
    echo   Put it in: tools\oxipng\oxipng.exe
    pause
    exit /b
)

set "TOTAL_BEFORE=0"
set "TOTAL_AFTER_OX=0"
set "COUNT=0"

REM === Pass 1: lossless (Oxipng) ===
echo [1/2] Lossless optimization (Oxipng -o 6)...
echo.

for /r "datadevice\images" %%f in (*.png) do (
    for %%A in ("%%f") do set /a "SB=%%~zA"
    "%OXIPNG%" -o 6 --strip safe "%%f" >nul 2>&1
    for %%A in ("%%f") do set /a "SA=%%~zA"
    set /a "TOTAL_BEFORE+=SB"
    set /a "TOTAL_AFTER_OX+=SA"
    set /a "COUNT+=1"

    set /a "PROGRESS=COUNT %% 20"
    if !PROGRESS! == 0 echo   Processed: !COUNT! files
)

set /a "SAVED_OXIPNG=TOTAL_BEFORE-TOTAL_AFTER_OX"
echo   Files: !COUNT!
echo   After Oxipng: !TOTAL_AFTER_OX! bytes ^(saved !SAVED_OXIPNG!^)
echo.

if not defined PNGQUANT goto :no_pngquant

REM === Pass 2: lossy (Pngquant) ===
echo [2/2] Lossy optimization (Pngquant 65-90)...
echo.

set "TOTAL_AFTER_PQ=0"
set "COUNT_PQ=0"

for /r "datadevice\images" %%f in (*.png) do (
    "%PNGQUANT%" --ext .png --force --skip-if-larger --quality=65-90 --speed 1 "%%f" >nul 2>&1
    for %%A in ("%%f") do set /a "TOTAL_AFTER_PQ+=%%~zA"
    set /a "COUNT_PQ+=1"
)

set /a "SAVED_PQ=TOTAL_AFTER_OX-TOTAL_AFTER_PQ"
set "FINAL_SIZE=!TOTAL_AFTER_PQ!"

echo   Files: !COUNT_PQ!
echo   After Pngquant: !TOTAL_AFTER_PQ! bytes ^(saved !SAVED_PQ!^)
echo.
goto :summary

:no_pngquant
echo [2/2] Pngquant not found - skipping lossy optimization
echo       Download: https://pngquant.org/
echo.
set "SAVED_PQ=0"
set "FINAL_SIZE=!TOTAL_AFTER_OX!"

:summary
set /a "TOTAL_SAVED=TOTAL_BEFORE-FINAL_SIZE"
if !TOTAL_BEFORE! GTR 0 (
    set /a "TOTAL_PERCENT=TOTAL_SAVED*100/TOTAL_BEFORE"
) else (
    set /a "TOTAL_PERCENT=0"
)

color 07
echo ========================================
echo   OK MAXIMUM OPTIMIZATION COMPLETED
echo ========================================
echo   Original size: !TOTAL_BEFORE! bytes
echo   Final size:    !FINAL_SIZE! bytes
echo   Total saved:   !TOTAL_SAVED! bytes (!TOTAL_PERCENT!%%)
echo ========================================
echo.
echo Note: Pngquant uses lossy compression.
echo If quality matters, use scripts\optimize_images.bat instead.
echo.
pause
