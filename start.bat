@echo off
chcp 65001 >nul
title ModiPAD - Main Menu
color 07
REM Single root entry point: full menu. Action scripts live in scripts\,
REM per-emulator scripts in simulator\, helper engines in tools\utils\.
cd /d "%~dp0"
set "PROJECT_DIR=%CD%"

:MENU
cls
echo ================================================================================
echo        MODIPAD - MAIN MENU
echo ================================================================================
echo.
echo   --- СБОРКА (BUILD) ------------
echo   [1]  Build firmware             (Собрать только прошивку)
echo   [2]  Build storage              (Собрать только внутреннее хранилище LittleFS)
echo   [3]  Build firmware + storage   (Собрать прошивку + внутреннее хранилище LittleFS)
echo   [4]  Flash firmware             (Залить только прошивку)
echo   [5]  Flash storage              (Залить только внутреннее хранилище, uploadfs)
echo   [6]  Flash firmware + storage   (Залить прошивку + внутреннее хранилище)
echo   [7]  Upload SD card             (Залить медиа на SD-карту в \modipad\)
echo.
echo   [8]  Serial monitor      (Монитор последовательного порта, 115200)
echo   [9]  Open Web UI         (Открыть http://192.168.4.1 - нужна сеть ModiPAD_Setup)
echo   [10] Clean project       (Удаляет папку .pio - сборка и кеш зависимостей)
echo.
echo   --- ЭМУЛЯТОР (EMULATOR) ---
echo   [11] Emulator: SDL2      (UI-симулятор в окне SDL2 - нужен tools\sdl2\bin\SDL2.dll)
echo   [12] Emulator: Win32     (UI-симулятор на чистом Win32 API, без DLL)
echo   [13] Web UI preview      (Редактор конфига сохраняет в datadevice\config.json. После выполнить [5])
echo.
echo   --- РЕСУРСЫ ----------------------
echo   [14] Generate images             (Сгенерировать PNG-библиотеку и сжать без потерь)
echo   [15] Optimize PNG - lossless     (Сжать PNG без потери качества: Oxipng, ECT или OptiPNG)
echo   [16] Optimize PNG - max lossy    (Максимальное сжатие PNG с потерями: Pngquant)
echo   [17] Generate fonts              (Сгенерировать шрифты 10/12/14/16/18 и bold через lv_font_conv)
echo   [18] Compress SD backgrounds     (Сжатие фонов на карте: datasdcard\modipad\backgrounds)
echo.
echo   [0]  Выход
echo.
echo ================================================================================
echo.
set /p choice="Выберите действие: "

if "%choice%"=="1"  call "%~dp0scripts\build.bat"
if "%choice%"=="2"  call "%~dp0scripts\build_fs.bat"
if "%choice%"=="3"  call "%~dp0scripts\build_all.bat"
if "%choice%"=="4"  call "%~dp0scripts\flash_firmware.bat"
if "%choice%"=="5"  call "%~dp0scripts\flash_fs.bat"
if "%choice%"=="6"  call "%~dp0scripts\flash.bat"
if "%choice%"=="7"  call "%~dp0scripts\sync_sd.bat"
if "%choice%"=="8"  call "%~dp0scripts\monitor.bat"
if "%choice%"=="9"  call "%~dp0scripts\open_web.bat"
if "%choice%"=="10" call "%~dp0scripts\clean.bat"
if "%choice%"=="11" call "%~dp0simulator\run_simulator.bat"
if "%choice%"=="12" call "%~dp0simulator\run_win32_simulator.bat"
if "%choice%"=="13" call "%~dp0scripts\run_web_preview.bat"
if "%choice%"=="14" call "%~dp0scripts\generate_images.bat"
if "%choice%"=="15" call "%~dp0scripts\optimize_images.bat"
if "%choice%"=="16" call "%~dp0scripts\optimize_max.bat"
if "%choice%"=="17" call "%~dp0scripts\generate_fonts.bat"
if "%choice%"=="18" call "%~dp0scripts\compress_backgrounds.bat"
if "%choice%"=="0" exit

goto MENU
