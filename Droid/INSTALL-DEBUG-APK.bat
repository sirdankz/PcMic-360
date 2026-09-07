@echo off
setlocal
cd /d "%~dp0"
set "APK=app\build\outputs\apk\debug\app-debug.apk"
if not exist "%APK%" (
  echo APK not found. Run BUILD-DEBUG-APK-WINDOWS.bat first.
  pause
  exit /b 1
)
where adb >nul 2>&1
if errorlevel 1 (
  echo adb was not found in PATH. Use Android Studio Device Manager or copy the APK to your phone manually.
  pause
  exit /b 1
)
adb install -r "%APK%"
pause
