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
echo   --- УСТРОЙСТВО -------------
echo   [1]  Build firmware        (Собрать прошивку, ничего не заливается)
echo   [2]  Flash firmware only   (Залить только прошивку)
echo   [3]  Flash fw + storage    (Залить прошивку и внутреннее хранилище LittleFS)
echo   [4]  Upload storage only   (Залить только внутреннее хранилище, uploadfs)
echo   [5]  Upload SD card        (Залить медиа на SD-карту в \modipad\)
echo   [6]  Flash ALL             (Прошивка + внутреннее хранилище + SD-карта)
echo   [7]  Serial monitor        (Монитор последовательного порта, 115200)
echo   [8]  Open Web UI           (Открыть http://192.168.4.1 - нужна сеть ModiPAD_Setup)
echo   [9]  Clean project         (Удаляет папку .pio - сборка и кеш зависимостей)
echo.
echo   --- ЭМУЛЯТОР / ВЕБ ---------
echo   [10] Emulator: SDL2        (UI-симулятор в окне SDL2 - нужен tools\sdl2\bin\SDL2.dll)
echo   [11] Emulator: Win32       (UI-симулятор на чистом Win32 API, без DLL)
echo   [12] Web UI preview        (Редактор конфига в браузере - сохраняет в datadevice\config.json)
echo                                После редактирования: [4] Upload storage only - зальёт конфиг на устройство
echo.
echo   --- РЕСУРСЫ ----------------------
echo   [13] Generate images             (Сгенерировать PNG-библиотеку и сжать без потерь)
echo   [14] Optimize PNG - lossless     (Сжать PNG без потери качества: Oxipng, ECT или OptiPNG)
echo   [15] Optimize PNG - max lossy    (Максимальное сжатие PNG с потерями: Pngquant)
echo   [16] Generate fonts              (Сгенерировать шрифты 10/12/14/16/18 и bold через lv_font_conv)
echo   [17] Compress SD backgrounds     (Сжатие фонов на карте: datasdcard\modipad\backgrounds)
echo.
echo   [0]  Выход
echo.
echo ================================================================================
echo.
set /p choice="Выберите действие: "

if "%choice%"=="1"  call "%~dp0scripts\build.bat"
if "%choice%"=="2"  call "%~dp0scripts\flash_firmware.bat"
if "%choice%"=="3"  call "%~dp0scripts\flash.bat"
if "%choice%"=="4"  call "%~dp0scripts\upload_files_only.bat"
if "%choice%"=="5"  call "%~dp0scripts\sync_sd.bat"
if "%choice%"=="6"  call "%~dp0scripts\flash_all.bat"
if "%choice%"=="7"  call "%~dp0scripts\monitor.bat"
if "%choice%"=="8"  call "%~dp0scripts\open_web.bat"
if "%choice%"=="9"  call "%~dp0scripts\clean.bat"
if "%choice%"=="10" call "%~dp0simulator\run_simulator.bat"
if "%choice%"=="11" call "%~dp0simulator\run_win32_simulator.bat"
if "%choice%"=="12" call "%~dp0scripts\run_web_preview.bat"
if "%choice%"=="13" call "%~dp0scripts\generate_images.bat"
if "%choice%"=="14" call "%~dp0scripts\optimize_images.bat"
if "%choice%"=="15" call "%~dp0scripts\optimize_max.bat"
if "%choice%"=="16" call "%~dp0scripts\generate_fonts.bat"
if "%choice%"=="17" call "%~dp0scripts\compress_backgrounds.bat"
if "%choice%"=="0" exit

goto MENU
