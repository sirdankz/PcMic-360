#!/usr/bin/env python3
import socket
import time

HOST = "127.0.0.1"
PORT = 36000

with socket.create_connection((HOST, PORT), timeout=3) as s:
    s.settimeout(1)
    time.sleep(0.1)
    print(s.recv(8192).decode("ascii", "replace"), end="")
    s.sendall(b"P\nO\nS\n")
    for _ in range(3):
        s.sendall(b"\xA5\x70" + b"\x00" * 640)
    time.sleep(0.1)
    print(s.recv(8192).decode("ascii", "replace"), end="")
    s.sendall(b"T\nQ\n")
    time.sleep(0.1)
    try:
        print(s.recv(8192).decode("ascii", "replace"), end="")
    except Exception:
        pass
print("PROTOCOL_SMOKE_TEST_DONE")
