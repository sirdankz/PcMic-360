# Changelog

## PC GUI v1.5

Goal: reduce intermittent crackle/pops caused by capture callback overruns, Python scheduling jitter, or TCP stalls.

- Replaced PortAudio callback processing with a dedicated blocking capture thread.
- Capture thread requests Windows MMCSS `Pro Audio` scheduling when available.
- PCM gain, input level, RMS, gate, mute and endian conversion run outside PortAudio's real-time callback.
- Audio queue reduced from 16 frames (up to 320 ms) to 3 frames.
- Sender drains accumulated backlog and keeps only the newest microphone frame.
- Frames older than 80 ms are discarded rather than replayed late.
- Added capture overflow, capture jitter, network send-gap, maximum frame age and stale/queue-drop diagnostics.
- GUI accepts PcMic XEX v7.x HELLO banners, so v1.5 recognizes v7.03 and v7.04 handshakes.
- Existing input level, mic gain, voice gate, mute/hotkeys and safe-unload behavior remain.

Defaults:

```text
Input Level: 100%
Mic Gain: 1.00×
Queue: 3 × 20 ms frames maximum
Stale frame cutoff: 80 ms
```

## PC GUI v1.4

- Added **Mic Input Level** slider: 0–100%.
- Added **Mic Gain** slider: 0.00×–4.00×.
- Digital boost is clipped to the PCM16 range.
- RMS meter and Voice Gate see the processed signal after input level/gain.
- Input level and gain are saved in the existing PcMic settings file.

Signal order:

```text
microphone → Input Level → Mic Gain → RMS meter → Voice Gate → Mute → Xbox
```

## XEX v7.04

Goal: reduce unnecessary Xbox CPU work in the virtual-headset/no-wire path.

The v7.03 direct virtual path still ran the older wired-headset refill detector every 1 ms. With multiple tracked 640-byte slots, that meant repeated hashing, validity checks and slot bookkeeping even though virtual/no-wire mode has no controller DMA refill edge to discover.

Changes:

1. `V700Poll` immediately returns in virtual-headset + no-physical-headset mode.
2. Direct virtual injection no longer computes the 640-byte post-submit hash or tracks refill slots.
3. Wired/physical-headset behavior keeps the older refill hash/slot path.
4. Candidate scoring stops once lane/geometry is locked.
5. No-wire network loop sleeps 2 ms instead of 1 ms; wired mode keeps the 1 ms polling behavior.
6. `V700_STATUS` reports `perf=VIRTUAL_DIRECT_NO_POLL` or `perf=WIRED_REFILL_POLL`.
7. Protocol, TCP port 36000, dynamic resolver, virtual headset, PCM geometry and commands remain unchanged.
