@echo off
setlocal
cd /d "%~dp0"

set "GRADLE_VERSION=8.13"
set "GRADLE_HOME_LOCAL=%CD%\.build-tools\gradle-%GRADLE_VERSION%"
set "GRADLE_ZIP=%CD%\.build-tools\gradle-%GRADLE_VERSION%-bin.zip"

if not defined ANDROID_SDK_ROOT if defined ANDROID_HOME set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
if not defined ANDROID_SDK_ROOT (
  echo.
  echo ERROR: ANDROID_SDK_ROOT or ANDROID_HOME is not set.
  echo Install Android Studio and Android SDK Platform 36 first.
  echo Then either build from Android Studio or set the SDK environment variable.
  pause
  exit /b 1
)

if not exist "%ANDROID_SDK_ROOT%\platforms\android-36\android.jar" (
  echo.
  echo ERROR: Android SDK Platform 36 is not installed in:
  echo   %ANDROID_SDK_ROOT%
  echo Install SDK Platform 36 from Android Studio ^> SDK Manager.
  pause
  exit /b 1
)

if not exist "%GRADLE_HOME_LOCAL%\bin\gradle.bat" (
  echo Downloading Gradle %GRADLE_VERSION%...
  if not exist ".build-tools" mkdir ".build-tools"
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing 'https://services.gradle.org/distributions/gradle-%GRADLE_VERSION%-bin.zip' -OutFile '%GRADLE_ZIP%'"
  if errorlevel 1 goto :fail
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Expand-Archive -Force '%GRADLE_ZIP%' '%CD%\.build-tools'"
  if errorlevel 1 goto :fail
)

echo.
echo Building PcMic-360 Android debug APK...
call "%GRADLE_HOME_LOCAL%\bin\gradle.bat" --no-daemon assembleDebug
if errorlevel 1 goto :fail

echo.
echo BUILD COMPLETE:
echo   app\build\outputs\apk\debug\app-debug.apk
explorer "app\build\outputs\apk\debug"
pause
exit /b 0

:fail
echo.
echo BUILD FAILED.
pause
exit /b 1
