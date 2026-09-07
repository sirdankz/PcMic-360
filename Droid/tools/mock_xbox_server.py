#!/usr/bin/env python3
"""PcMic-360 Android mock Xbox server.

Listens on TCP 36000, emits the v7.03 HELLO/status lines, accepts P/O/S/T/Q,
and validates A5 70 + 640-byte audio frames. Useful for Android app testing
without an Xbox connected.
"""
import socket
import time

HOST = "0.0.0.0"
PORT = 36000
FRAME = 640


def send_line(c, text):
    c.sendall((text + "\r\n").encode("ascii"))
    print("->", text)


def session(c, addr):
    print("connected", addr)
    send_line(c, "VOICE_DECODE_UNIVERSAL_SYSTEM_MIC_V7_03 HELLO")
    send_line(c, "TRANSPORT EXACT_NOVA_V14_UNCHANGED")
    send_line(c, "MODE UNIVERSAL_DYNAMIC_XVOICED_VIRTUAL_HEADSET_V7_03_XBOX_LISTENER")
    send_line(c, "READY commands=O_install S_live T_stop P_report Q_restore_quit AUDIO=A570+640bytes_PCM16BE")

    installed = False
    live = False
    audio_state = 0
    audio = bytearray()
    frames = 0
    last_status = time.monotonic()
    c.settimeout(0.1)

    while True:
        try:
            data = c.recv(65536)
            if not data:
                return
        except socket.timeout:
            data = b""

        for b in data:
            if audio_state == 2:
                audio.append(b)
                if len(audio) == FRAME:
                    frames += 1
                    if frames == 1 or frames % 50 == 0:
                        print(f"AUDIO frame={frames} bytes={len(audio)}")
                    audio.clear()
                    audio_state = 0
                continue

            if audio_state == 1:
                if b == 0x70:
                    audio_state = 2
                    audio.clear()
                    continue
                audio_state = 0

            if b == 0xA5:
                audio_state = 1
                continue

            ch = chr(b).upper()
            if ch == "P":
                send_line(c, f"V700_DISCOVERY locked={1 if installed else 0} lane=1 bytes=0x280 f0c=0xA0 candidates=1 unexpectedGeometry=0")
            elif ch == "O":
                installed = True
                send_line(c, "V700_INSTALL ok=1 UNIVERSAL_SYSTEM_MIC=1 stub=0x81A70000 real=0x80102048 hook=0x90000000 lane=AUTO refill=AUTO")
            elif ch == "S":
                live = True
                send_line(c, "V700_START ok=1 mode=2 strategy=DYNAMIC_LANE_POSTCALL_BASELINE_REFILL_EDGE waitingForLane=0")
            elif ch == "T":
                live = False
                send_line(c, "V700_STOP oldMode=2 locked=1 lane=1 bytes=0x280 f0c=0xA0")
            elif ch == "Q":
                live = False
                installed = False
                send_line(c, "V700_RESTORE ok=1 stub=0x81A70000 real=0x80102048 headsetStub=0x81A71000")
                send_line(c, "SAFE_TO_UNLOAD")
                return

        now = time.monotonic()
        if now - last_status >= 1.0:
            last_status = now
            send_line(
                c,
                "V700_STATUS "
                f"installed={1 if installed else 0} mode={2 if live else 0} "
                f"locked={1 if installed else 0} lane=1 bytes=0x280 f0c=0xA0 "
                "physicalHeadset=0 virtualHeadset=1"
            )


def main():
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(5)
        print(f"PcMic-360 mock Xbox listening on {HOST}:{PORT}")
        while True:
            c, addr = s.accept()
            with c:
                try:
                    session(c, addr)
                except (ConnectionError, OSError) as e:
                    print("session ended:", e)


if __name__ == "__main__":
    main()
