#!/usr/bin/env python3
"""Minimal MFC-J5720DW emulator on localhost:54921 replaying recorded replies."""
import socket, sys, threading
STREAM = open(sys.argv[1], "rb").read()
def offer(csv):
    b = csv.encode() + b"\x00"
    return b"\x00" + len(b).to_bytes(2, "little") + b
ESCQ = bytes.fromhex("c100460bff3f0000000000000001040101010201030000000000000001015b08860b5b08d00d5b08d00db405b405b405b4050a000a00000101600960096009b004580258020f3200")
def handle(c):
    c.sendall(b"+OK 200\r\n"); src = "fb"; buf = b""
    while True:
        d = c.recv(4096)
        if not d: break
        buf += d
        while b"\x80" in buf or buf == b"\x1bR":
            frame, _, buf = buf.partition(b"\x80")
            if not frame.startswith(b"\x1b"): continue
            letter = frame[1:2]; body = frame[3:]
            if letter == b"Q": c.sendall(ESCQ)
            elif letter == b"S":
                src = "adf" if b"ADF" in body else "fb"; c.sendall(b"\x80")
            elif letter == b"D": c.sendall(b"\x80")   # acked but ignored, like the real unit
            elif letter == b"I":
                dpi = int(body.split(b"R=")[1].split(b",")[0])
                if src == "adf": c.sendall(offer(f"{dpi},{dpi},1,213,{2527*dpi//300},0,0,"))
                else: c.sendall(offer(f"{dpi},{dpi},2,213,{2527*dpi//300},295,{3484*dpi//300},"))
            elif letter == b"X":
                sys.stderr.write(f"ESC X body: {body!r}\n")
                c.sendall(STREAM if src == "adf" else b"\xc2")
    c.close()
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 54921)); s.listen(2); print("listening", flush=True)
while True:
    c, _ = s.accept(); threading.Thread(target=handle, args=(c,), daemon=True).start()
