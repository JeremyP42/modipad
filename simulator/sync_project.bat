@echo off
chcp 65001 >nul
REM The simulator now reads/writes the project's REAL data folders directly:
REM   /littlefs/... -> ../datadevice/...
REM   /sdcard/...   -> ../datasdcard/...
REM (see simulator/src/host_stubs.cpp), so there is nothing to copy any more.
REM This script is kept because the launcher scripts call it before building.
cd /d "%~dp0"
echo Using project data in place: ..\datadevice and ..\datasdcard
