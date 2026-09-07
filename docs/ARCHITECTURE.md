# Architecture

## Normal workflow

```text
Xbox 360
  |
  | load PcMic-360.xex
  v
TCP listener :36000
  ^
  |
  | user enters Xbox IP
  |
PcMic-360.exe
  |
  +--> CONNECT
          |
          +--> HELLO v7.x detected
          |
          +--> request current status (P)
          |
          +--> hook already installed? ---- YES ---> ready
          |                          |
          |                          NO
          |                          v
          +------------------------> automatic O install
                                      |
                                      v
                                HOOK INSTALLED
                                      |
                                      v
                              START LIVE MIC
                                      |
                                      v
PC mic -> processing -> PCM packets -> Xbox system voice path
```

## Audio format

```text
Sample rate:       16,000 Hz
Channels:          1 (mono)
Sample format:     signed PCM16
Wire byte order:   big-endian
Frame duration:    20 ms
Samples/frame:     320
PCM bytes/frame:   640
TCP port:          36000
```

## Host processing

```text
Microphone
  ↓
Input Level
  ↓
Digital Gain
  ↓
RMS meter
  ↓
Voice Gate
  ↓
Mute / hold state
  ↓
PCM16-BE frame
  ↓
PcMic TCP transport
```

GUI v1.5 intentionally favors the newest microphone frame over replaying stale queued frames after a host/network stall.

## Xbox voice path

The XEX dynamically resolves the system voice path and observes the expected packet geometry rather than depending on one title's fixed microphone offsets. The virtual-headset path allows injection without a physical controller headset when the relevant system voice calls are available.

XEX v7.04 retains the older wired refill-tracking path for physical-headset mode, while the virtual/no-wire path uses direct injection and skips the unnecessary refill polling loop.
