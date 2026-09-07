PcMic-360 Android v1.0 - Protocol Map
=====================================

ANDROID PHONE                         XBOX 360 / PcMic-360.xex
-------------                         ------------------------
                                      TCP LISTEN :36000
     user enters Xbox IP
              |
              +-------- TCP CONNECT ----------------------->
                                                        HELLO
              <---------------------------------------------+
              |
              +-------- "P\\n" status --------------------->
              |
       installed=0 ?
              |
              +-------- "O\\n" auto-install --------------->
              <--------- V700_INSTALL ok=1 -----------------+

START LIVE MIC
      |
      +---------------- "S\\n" ----------------------------->
      |
 AudioRecord
      |
 20 ms input frame
      |
 optional resample
      |                  exact output format
      +---------------> 16000 Hz mono PCM16
                         320 samples = 640 bytes
      |
 RMS gate + 180 ms hangover
      |
 mute / hold state
      |
 gate closed or muted => 640 x 00
      |
 convert PCM16 to BIG ENDIAN
      |
      +-------- A5 70 + 640-byte frame --------------------->
      +-------- A5 70 + 640-byte frame ---------------------> every ~20 ms
      +-------- A5 70 + 640-byte frame --------------------->

STOP LIVE MIC
      +---------------- "T\\n" ----------------------------->

REPORT STATUS
      +---------------- "P\\n" ----------------------------->
      <---------------- V700_STATUS ... ---------------------+

RESTORE / SAFE UNLOAD
      +---------------- "Q\\n" ----------------------------->
      <---------------- SAFE_TO_UNLOAD ----------------------+
      |
      +-- auto reconnect disabled until user deliberately connects again
