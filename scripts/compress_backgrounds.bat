@echo off
chcp 65001 >nul
title ModiPAD - Compress Backgrounds
color 07
echo ========================================
echo   MODIPAD - COMPRESS BACKGROUNDS
echo ========================================
echo.
echo  Aggressive background compression for 480x320 panels.
echo  Methods: jpg (q30-40 progressive) | png8 (64/128 colors + dither)
echo.

cd /d "%~dp0.."

set "PY=py -3"
where py >nul 2>&1 || set "PY=python"

REM Default: in-place, PNG-8 / 128 colors over datasdcard\modipad\backgrounds
REM Pass extra args, e.g.:  compress_backgrounds.bat --method both
REM                         compress_backgrounds.bat --method jpg --quality 35
%PY% "tools\utils\compress_backgrounds.py" -i "datasdcard\modipad\backgrounds" %*

echo.
echo Done.
pause
