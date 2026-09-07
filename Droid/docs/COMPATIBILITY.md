# Compatibility

PcMic-360 DROID v1.3 is the Android client for the PcMic-360 v7.x TCP/audio protocol.

For the stable public repository, use it with the known-working **PcMic-360 XEX v7.04** branch.

Network direction:

- Xbox 360 listens on TCP port `36000`.
- Android connects to the Xbox's local IP address.

Audio framing:

- Prefix: `A5 70`
- Payload: 640 bytes
- Format: 16 kHz, mono, PCM16 big-endian
- Frame duration: 20 ms / 320 samples

The Android app does not require changes to the Xbox-side voice hook implementation.
