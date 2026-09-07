# GitHub / release checklist

## Repository About description

Use this for the GitHub **About** field:

> Use a Windows PC microphone as an Xbox 360 voice-chat mic on homebrew-enabled consoles. Low-latency LAN audio, dynamic XAM/XVoiced hooking, virtual-headset support, voice gate, mute/hotkeys, input level and mic gain.

Suggested topics:

```text
xbox-360
xbox360
homebrew
rgh
jtag
xex
voice-chat
microphone
python
powerpc
xam
xvoiced
```

## Recommended release title

```text
PcMic-360 v1.5 (XEX v7.04)
```

## Recommended user release ZIP

```text
PcMic-360-v1.5-v7.04/
├─ PcMic-360.exe
├─ PcMic-360.xex
└─ README.md
```

Keep source and binaries separate: source belongs in the repository/source archive; compiled user binaries are better attached to a GitHub Release.

## Before publishing binaries

- Build the Windows EXE from the committed v1.5 source.
- Build the XEX from the committed v7.04 source.
- Test the EXE on a Windows PC without Python installed.
- Verify Live Mic, mute, voice gate, input level and gain.
- Verify `perf=VIRTUAL_DIRECT_NO_POLL` with no physical headset.
- Verify **RESTORE / SAFE UNLOAD** produces `SAFE_TO_UNLOAD` before manually unloading.
- Generate SHA-256 hashes from the **final binaries**, then paste them into the GitHub Release notes.

Example PowerShell commands:

```powershell
Get-FileHash .\PcMic-360.exe -Algorithm SHA256
Get-FileHash .\PcMic-360.xex -Algorithm SHA256
```

The old source-tree `SHA256SUMS.txt` was intentionally removed because it referenced outdated v1.4 files.

## GitHub release notes draft

```markdown
## PcMic-360 v1.5 / XEX v7.04

PcMic-360 lets a Windows PC microphone feed the Xbox 360 system voice-chat path over your local network.

### Highlights
- Automatic dynamic voice-hook install after connection
- Virtual-headset/no-physical-headset support
- Mic Input Level and digital Mic Gain
- RMS voice gate, mute and global M/Space controls
- PC v1.5 capture/jitter/backlog improvements
- XEX v7.04 virtual/no-wire CPU optimization
- Runtime audio and XEX diagnostics

### Important
Before manually unloading `PcMic-360.xex`, use **RESTORE / SAFE UNLOAD** and wait for `SAFE_TO_UNLOAD`.
```
