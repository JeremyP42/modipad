@echo off
chcp 65001 >nul
title ModiPAD - SDL2 Simulator
color 07
echo.
echo ========================================
echo   MODIPAD - SDL2 SIMULATOR
echo ========================================
echo.

cd /d "%~dp0"

set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

REM MinGW toolchain (gcc/g++) is not bundled with the 'native' platform.
where gcc >nul 2>&1 || set "PATH=%USERPROFILE%\.platformio\packages\toolchain-gccmingw32\bin;%PATH%"

echo [1/3] Checking local SDL2...
if not exist "..\tools\sdl2\bin\SDL2.dll" (
    color 07
    echo X Not found: tools\sdl2\bin\SDL2.dll
    echo   Put the 32-bit ^(i686^) SDL2 dev files there.
    pause
    exit /b
)

echo [2/3] Syncing data + sources, then building (native)...
call "%~dp0sync_project.bat"
"%PIO%" run -e native
if %errorlevel% neq 0 (
    color 07
    echo.
    echo X Build failed.
    echo   - "undefined reference to X" -^> add X to src\host_stubs.cpp
    echo   - "SDL.h: No such file"     -^> check tools\sdl2\include
    echo   - "skipping incompatible ...SDL2" -^> SDL2 must be 32-bit ^(i686^)
    echo.
    pause
    exit /b
)

echo.
echo [3/3] Running (mouse = touch, drag = swipe)...
if not exist ".pio\build\native" mkdir ".pio\build\native"
copy /y "..\tools\sdl2\bin\SDL2.dll" ".pio\build\native\" >nul
".pio\build\native\program.exe"
pause
