#!/usr/bin/env python3
"""Raw ADF probe for a Brother scanner on TCP 54921 (protocol per docs/PROTOCOL.md).
Dumps every reply so a model's deviations from the J6920DW can be seen."""
import socket, sys, time

HOST = sys.argv[1]
SOURCE = sys.argv[2] if len(sys.argv) > 2 else "adf"
DPI = int(sys.argv[3]) if len(sys.argv) > 3 else 100
OUT = sys.argv[4] if len(sys.argv) > 4 else "/tmp/adf_probe.bin"

def cmd(letter, body=None):
    b = b"\x1b" + letter + b"\n"
    if body: b += body + b"\n"
    return b + b"\x80"

def read_quiet(s, quiet=1.0, total=30):
    s.settimeout(quiet); buf = b""; t0 = time.time()
    while time.time() - t0 < total:
        try:
            chunk = s.recv(65536)
            if not chunk: break
            buf += chunk
        except socket.timeout:
            if buf: break
    return buf

s = socket.create_connection((HOST, 54921), timeout=10)
print("greeting:", read_quiet(s, 0.5))
s.sendall(cmd(b"Q")); r = read_quiet(s, 0.5); print(f"ESC Q ({len(r)}B):", r.hex())
s.sendall(cmd(b"S", b"FB")); r = read_quiet(s, 0.5); print("ESC S FB:", r.hex())
if SOURCE == "adf":
    s.sendall(cmd(b"S", b"ADF")); r = read_quiet(s, 0.5); print("ESC S ADF:", r.hex(), "(80=loaded, c2=empty)")
s.sendall(cmd(b"I", f"R={DPI},{DPI}\nM=CGRAY\nD=SIN".encode())); r = read_quiet(s, 0.5)
print("ESC I:", r[:3].hex(), r[3:].decode("ascii", "replace"))
offer = r[3:].decode("ascii", "replace").strip("\x00").split(",")
xmax, ymax = int(offer[4]), int(offer[6])
AREA = sys.argv[5] if len(sys.argv) > 5 else None
if AREA is None and ymax == 0:  # Loaded ADF: unknown length -> ask for Legal (14 in).
    ymax = 14 * DPI
    print("ADF loaded (ymax=0); requesting", ymax, "rows")
print(f"offer xmax={xmax} ymax={ymax} at {DPI} dpi -> {xmax*300//DPI}x{ymax*300//DPI} @300")
area = AREA or f"0,0,{xmax},{ymax}"
body = f"B=50\nN=50\nM=CGRAY\nC=JPEG\nJ=MID\nR={DPI},{DPI}\nA={area}\nD=SIN\nP=0\nE=1\nG=0".encode()
print("ESC X body:", body); s.sendall(cmd(b"X", body))
data = read_quiet(s, 30.0, 180)
open(OUT, "wb").write(data)
print(f"ESC X reply: {len(data)} bytes -> {OUT}")
print("first 64:", data[:64].hex(" "))
print("last 32:", data[-32:].hex(" "))
