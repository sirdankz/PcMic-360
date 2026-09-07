@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if errorlevel 1 (
    echo ERROR: Python launcher ^(py.exe^) was not found.
    echo Install 64-bit Python for Windows first.
    pause
    exit /b 1
)

py -m pip install --disable-pip-version-check numpy sounddevice
if errorlevel 1 (
    echo Failed to install required packages.
    pause
    exit /b 1
)

start "" pyw "PcMic-360-GUI-v1.5.pyw"
