PcMic-360 Android v1.3 - First Test

1. Build/install PcMic-360.apk.
2. Load the same working PcMic-360.xex.
3. Connect to the Xbox IP.
4. Confirm HOOK checkmark appears.
5. Select microphone.
6. Start Live Mic.

CONTROL COMMAND TEST
--------------------
Start Live Mic:
- log must NOT contain NetworkOnMainThreadException
- Xbox should report V700_START ok=1 mode=2

Stop Live Mic:
- Xbox should report V700_STOP
- later V700_STATUS should show mode=0

VOICE GATE TEST
---------------
- Speak and watch the displayed live RMS.
- Move threshold ABOVE the displayed RMS.
- log should show:
  Voice gate CLOSED ... sending exact digital silence.
- audio should stop passing.
- Move threshold BELOW speech RMS.
- log should show Voice gate OPEN.

RESTORE TEST
------------
- Tap RESTORE HOOKS (SAFE TO UNLOAD)
- expect:
  Restore command queued...
  XEX -> V700_STOP ...
  XEX -> V700_RESTORE ...
  XEX -> SAFE_TO_UNLOAD
- app should then display SAFE.
- If SAFE_TO_UNLOAD is not received, do not unload the XEX.

COPY LOG and send the complete log if any step differs.
