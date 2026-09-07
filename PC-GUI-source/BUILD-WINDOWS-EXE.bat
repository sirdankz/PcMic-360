@echo off
setlocal
cd /d "%~dp0"

echo ============================================================
echo   PcMic-360 - GUI v1.5 EXE Builder
echo ============================================================
echo.

where py >nul 2>nul
if errorlevel 1 (
    echo ERROR: Python launcher ^(py.exe^) was not found.
    echo Install 64-bit Python for Windows, then run this again.
    pause
    exit /b 1
)

echo [1/3] Installing/updating build dependencies...
py -m pip install --upgrade --disable-pip-version-check pyinstaller numpy sounddevice
if errorlevel 1 goto :fail

echo.
echo [2/3] Building windowed single-file EXE...
py -m PyInstaller --noconfirm --clean --onefile --windowed ^
  --name "PcMic-360" ^
  --icon "PcMic-360.ico" ^
  --add-data "PcMic-360.ico;." ^
  --collect-all sounddevice ^
  "PcMic-360-GUI-v1.5.py"
if errorlevel 1 goto :fail

echo.
echo [3/3] DONE
echo EXE: %CD%\dist\PcMic-360.exe
echo.
explorer "%CD%\dist"
pause
exit /b 0

:fail
echo.
echo BUILD FAILED. Read the error above.
pause
exit /b 1
