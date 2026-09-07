# PcMic-360 DROID v1.3

Android client source for **PcMic-360**. This project captures microphone audio on Android and streams it over the local network to the PcMic-360 Xbox 360 XEX receiver.

## Build without Android Studio

On Windows, run:

```text
BUILD-APK-NO-ANDROID-STUDIO.bat
```

The builder prepares the required command-line Android build environment and builds the APK from this source tree.

For additional builder details, see [`docs/BUILDING-WITHOUT-ANDROID-STUDIO.md`](docs/BUILDING-WITHOUT-ANDROID-STUDIO.md).

## Android Studio

The folder is also a normal Gradle Android project and can be opened in Android Studio if preferred.

## Compatibility

This Android client is intended for the PcMic-360 v7.x XEX protocol used by the main PcMic-360 project. See [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) and [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Permissions

See [`docs/PRIVACY.md`](docs/PRIVACY.md) for the permissions used by the app and why they are required.

## Source

The application source is under:

```text
app/src/main/java/com/pcmic360/app/
```

The portable command-line build helper scripts are under:

```text
tools/build/
```

PcMic-360 is distributed under the license of the parent repository.

## Additional documentation

- [Compatibility](docs/COMPATIBILITY.md)
- [Protocol](docs/PROTOCOL.md)
- [Privacy & permissions](docs/PRIVACY.md)
- [Testing checklist](docs/TESTING.md)
- [v1.3 changes](docs/CHANGELOG-v1.3.md)

