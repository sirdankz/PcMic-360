PcMic-360 Android v1.3

WHY THIS BUILD EXISTS
---------------------
A real-device log showed Android throwing NetworkOnMainThreadException when sending:
  S = Start Live Mic
  T = Stop Live Mic
  Q = Restore / Safe Unload

That meant the microphone audio thread could continue working while the Xbox control command
never reached the console. In particular, Q could fail completely, leaving the Xbox at
installed=1 mode=2.

FIX 1 - BACKGROUND COMMAND WRITER
---------------------------------
P/O/S/T/Q now enter a dedicated command queue.
PcMic-Outbound is the only thread that writes commands or audio to the socket.
Commands are always sent before queued audio frames.

FIX 2 - RESTORE CONFIRMATION
----------------------------
Restore:
  1. stops Android capture
  2. clears queued mic audio
  3. queues Q on the background writer
  4. waits for the literal SAFE_TO_UNLOAD response
  5. disables reconnect
  6. lets the network loop close the socket on its background thread

The app never reports SAFE unless SAFE_TO_UNLOAD was actually received.
This restores the Xbox voice hooks; it does not remove the XEX from memory.
Unload the XEX from the Xbox loader only after SAFE is shown.

FIX 3 - VOICE GATE
------------------
Old slider: 0-2000 RMS
New slider: 0-20000 RMS

Some Android VOICE_COMMUNICATION capture paths apply aggressive AGC, so ordinary speech or
even room noise can exceed RMS 2000. That made the old maximum threshold ineffective.

v1.3:
- prefers MediaRecorder.AudioSource.MIC before VOICE_COMMUNICATION
- shows live RMS beside GATE OPEN/CLOSED
- logs every gate transition
- clears queued speech on gate close
- sends exact 640-byte digital silence while closed
- uses a pure GateLogic implementation with unit tests

TEST SIGNALS
------------
If threshold = 5000 and live RMS = 800:
  GATE CLOSED - silence sent

If threshold = 5000 and live RMS = 7000:
  GATE OPEN - microphone sent

If threshold = 0:
  GATE OFF - microphone always passes unless muted
