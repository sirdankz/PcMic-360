PcMic-360 Android v1.1 — Privacy / Permissions
================================================

PcMic-360 is an unofficial Xbox 360 homebrew companion app.

RUNTIME PERMISSION (the only popup the app requests)
------------------------------------------------------
Microphone
  Requested only when START LIVE MIC is pressed.
  Needed to capture the microphone input selected by the user.

NORMAL ANDROID CAPABILITIES (no runtime popup)
-----------------------------------------------
INTERNET
  Used only to open the TCP connection to the Xbox IP the user enters,
  on PcMic-360 port 36000.

FOREGROUND_SERVICE + FOREGROUND_SERVICE_MICROPHONE
  Required by Android so Live Mic may keep running as a visible
  microphone foreground service when the app is not in front.

NOT REQUESTED / NOT DECLARED
----------------------------
- Nearby Devices
- Location
- Bluetooth permissions
- Notifications runtime permission
- Contacts
- Photos / videos
- Files / storage
- Camera
- Phone / call logs
- SMS
- Advertising ID

The app contains no analytics, account login, ad SDK, telemetry, or cloud upload.
Microphone PCM is sent directly to the Xbox IP entered by the user.

Android 16 note
---------------
For ordinary Android 16 devices, outgoing LAN TCP still works with INTERNET.
Android 16's Local Network Protection is currently an opt-in compatibility
restriction. PcMic-360 intentionally does not request Nearby Devices just for
that experimental mode. A future Android release may require a dedicated local
network permission; if/when enforcement becomes standard, that can be handled
in a future build with a clear explanation to the user.
