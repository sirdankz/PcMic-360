# Testing checklist

## PC audio test

1. Build/run PC GUI v1.5.
2. Start with **Input Level 100%** and **Gain 1.00×**.
3. Start Live Mic and speak continuously for several minutes.
4. Watch **Audio Activity**.

Ideal counters:

```text
Capture overflow = 0
Stale/queue drops = 0 or very low
Max frame age well below 80 ms
Send gaps normally near 20 ms
```

Occasional operating-system scheduling jitter can occur. If a crackle/pop happens, capture the counters and event log immediately.

## Xbox performance test

1. Build/load XEX v7.04.
2. Connect with no physical headset.
3. Request status.
4. Confirm:

```text
perf=VIRTUAL_DIRECT_NO_POLL
```

5. Compare game performance with PcMic active vs. disabled/disconnected.
6. Confirm voice gate and mute still behave normally.
7. Use **RESTORE / SAFE UNLOAD** and wait for `SAFE_TO_UNLOAD` before manually unloading the XEX.

## Optional wired regression test

With a physical Xbox headset connected, status should use the wired refill path:

```text
perf=WIRED_REFILL_POLL
```

## Disconnect/unload safety test

For this stable v7.04 source, do not treat an unexpected TCP disconnect as proof the XEX is safe to unload. The tested safe manual sequence is:

```text
connected session
→ RESTORE / SAFE UNLOAD
→ SAFE_TO_UNLOAD
→ manual module unload
```
