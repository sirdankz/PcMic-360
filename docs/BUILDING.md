# Building PcMic-360 from source

This repository contains two independent build targets:

- **PC GUI v1.5** for Windows
- **XEX v7.04** for Xbox 360

The cleanup in this repository does **not** alter the working PC or XEX source behavior.

## Xbox 360 XEX

Open:

```text
XEX-source\PcMic-360.sln
```

Build with:

```text
Configuration: Release
Platform: Xbox 360
```

Expected output:

```text
XEX-source\build\Release\bin\PcMic-360.xex
XEX-source\build\Release\bin\PcMic-360.map
```

An Xbox 360 XDK / compatible Visual Studio Xbox 360 build environment is required. Microsoft SDK/XDK files are not included in this repository.

The included `xex.xml` intentionally remains the same as the tested v7.04 source. This GitHub cleanup does not add XDK/RGLoader portability changes or new socket privileges.

## Windows GUI

Run:

```text
PC-GUI-source\BUILD-WINDOWS-EXE.bat
```

The script installs/updates the build dependencies and creates a PyInstaller one-file windowed executable.

Expected output:

```text
PC-GUI-source\dist\PcMic-360.exe
```

For source testing without building an EXE:

```text
PC-GUI-source\RUN-GUI.bat
```

Python dependencies:

```text
numpy
sounddevice
```

## Release-build check

Before publishing a Windows executable, test `PcMic-360.exe` on a Windows machine that does **not** have Python installed. This confirms the one-file PyInstaller build contains its runtime dependencies.
