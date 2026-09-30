@echo off
title ModiPAD - Win32 Simulator
color 07
cd /d "%~dp0"

set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

REM MinGW toolchain is not bundled with the 'native' platform.
where gcc >nul 2>&1 || set "PATH=%USERPROFILE%\.platformio\packages\toolchain-gccmingw32\bin;%PATH%"

echo [1/2] Syncing data + sources, then building Win32 variant (native_win32)...
call "%~dp0sync_project.bat"
"%PIO%" run -e native_win32
if errorlevel 1 (
    echo [ERROR] Build failed. Check log above.
    echo   - "undefined reference to X" -^> add X to src\host_stubs.cpp
    pause
    exit /b
)

echo [2/2] Running...
echo       Mouse click = touch. Drag = swipe.
".pio\build\native_win32\program.exe"
pause
