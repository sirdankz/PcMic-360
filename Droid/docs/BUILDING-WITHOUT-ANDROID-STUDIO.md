PcMic-360 Android — Build Without Android Studio
=================================================

You do NOT need Android Studio.

EASIEST METHOD
--------------
1. Extract the whole PcMic-360 Android folder somewhere writable, such as Documents.
2. Double-click:

       BUILD-APK-NO-ANDROID-STUDIO.bat

3. The first run downloads portable build tools into .portable-tools inside this folder.
4. Read the Android SDK license link shown by the builder. Type I AGREE if you accept it.
5. The builder downloads and configures locally:
   - Eclipse Temurin JDK 17
   - Android SDK command-line tools
   - Android SDK Platform 36
   - Android Build Tools 36.0.0
   - Android Platform Tools
   - Gradle 8.13
6. It builds the app automatically.
7. Your APK appears at:

       OUTPUT\PcMic-360.apk

Nothing above is installed system-wide. Delete .portable-tools if you ever want to remove the build environment.

FIRST BUILD
-----------
The first build downloads several hundred MB and requires internet access.
Later builds reuse the local tools and are much faster.

INSTALLING THE APK
------------------
Copy OUTPUT\PcMic-360.apk to your Android phone and open it, or use ADB if you already have USB debugging enabled.

Android may ask you to allow installation from the app you used to open the APK. This is normal for an APK installed outside Google Play.

The app requests no runtime permission at launch. Microphone permission is requested only when START LIVE MIC is pressed.

XBOX SETUP
----------
Use the same PcMic-360.xex that works with the Windows version.
The Xbox listens on TCP 36000. In the Android app, enter the Xbox LAN/title IP and connect.
