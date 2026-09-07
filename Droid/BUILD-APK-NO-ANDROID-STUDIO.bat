@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PcMic-360 Android APK Builder

set "TOOLS=%CD%\.portable-tools"
set "JDK_HOME=%TOOLS%\jdk17"
set "SDK_ROOT=%TOOLS%\android-sdk"
set "GRADLE_VERSION=8.13"
set "GRADLE_HOME=%TOOLS%\gradle-%GRADLE_VERSION%"
set "CMDLINE_ZIP=%TOOLS%\commandlinetools-win-15859902_latest.zip"
set "CMDLINE_SHA=90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a"
set "OUTDIR=%CD%\OUTPUT"
set "HELPERS=%CD%\tools\build"

if not exist "%TOOLS%" mkdir "%TOOLS%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo.
echo ============================================================
echo                 PcMic-360 Android Builder
echo ============================================================
echo.
echo Android Studio is NOT required.
echo Everything is downloaded into:
echo   %TOOLS%
echo.
echo The first build downloads several hundred MB of build tools.
echo Later builds reuse them.
echo.
echo This builder downloads Google's Android SDK command-line tools.
echo Continuing requires acceptance of the Android SDK license terms:
echo   https://developer.android.com/studio/terms
echo.
set /p "AGREE=Type I AGREE to continue: "
if /I not "%AGREE%"=="I AGREE" (
    echo.
    echo Cancelled. Nothing was installed system-wide.
    pause
    exit /b 1
)

echo.
echo [1/6] Preparing portable JDK 17...
if not exist "%JDK_HOME%\bin\java.exe" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPERS%\Prepare-Jdk.ps1" -Tools "%TOOLS%" -JdkHome "%JDK_HOME%"
    if errorlevel 1 goto :fail
) else (
    echo JDK 17 already present.
)
set "JAVA_HOME=%JDK_HOME%"
set "PATH=%JAVA_HOME%\bin;%PATH%"
"%JAVA_HOME%\bin\java.exe" -version
if errorlevel 1 goto :fail

echo.
echo [2/6] Preparing Android SDK command-line tools...
if not exist "%SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPERS%\Prepare-AndroidCli.ps1" -Tools "%TOOLS%" -SdkRoot "%SDK_ROOT%" -ZipPath "%CMDLINE_ZIP%" -ExpectedSha "%CMDLINE_SHA%"
    if errorlevel 1 goto :fail
) else (
    echo Android command-line tools already present.
)
set "ANDROID_SDK_ROOT=%SDK_ROOT%"
set "ANDROID_HOME=%SDK_ROOT%"

if not exist "%SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" (
    echo ERROR: sdkmanager.bat was not created.
    goto :fail
)

echo.
echo [3/6] Accepting SDK component licenses...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPERS%\Accept-SdkLicenses.ps1" -SdkManager "%SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" -SdkRoot "%SDK_ROOT%"
if errorlevel 1 goto :fail

echo.
echo [4/6] Installing Android Platform 36 + Build Tools...
call "%SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" --sdk_root="%SDK_ROOT%" "platforms;android-36" "build-tools;36.0.0" "platform-tools"
if errorlevel 1 goto :fail

if not exist "%SDK_ROOT%\platforms\android-36\android.jar" (
    echo ERROR: Android Platform 36 did not install correctly.
    goto :fail
)

echo.
echo [5/6] Preparing Gradle %GRADLE_VERSION%...
if not exist "%GRADLE_HOME%\bin\gradle.bat" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPERS%\Prepare-Gradle.ps1" -Tools "%TOOLS%" -Version "%GRADLE_VERSION%" -GradleHome "%GRADLE_HOME%"
    if errorlevel 1 goto :fail
) else (
    echo Gradle already present.
)

> local.properties echo sdk.dir=%SDK_ROOT:\=/%

echo.
echo [6/6] Building PcMic-360 APK...
call "%GRADLE_HOME%\bin\gradle.bat" --no-daemon --console=plain --stacktrace assembleDebug
if errorlevel 1 goto :fail

set "BUILT=%CD%\app\build\outputs\apk\debug\app-debug.apk"
if not exist "%BUILT%" (
    echo ERROR: Gradle completed but app-debug.apk was not found.
    goto :fail
)

copy /Y "%BUILT%" "%OUTDIR%\PcMic-360.apk" >nul
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo BUILD COMPLETE!
echo ============================================================
echo.
echo Your installable APK is:
echo   %OUTDIR%\PcMic-360.apk
echo.
explorer "%OUTDIR%"
pause
exit /b 0

:fail
echo.
echo ============================================================
echo BUILD FAILED
echo ============================================================
echo.
echo Copy the error shown above and send it to me.
echo Build tools remain local in .portable-tools and can be deleted safely.
echo.
pause
exit /b 1
