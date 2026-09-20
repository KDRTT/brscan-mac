#!/usr/bin/env python3
import socket, sys, time
HOST=sys.argv[1]
def cmd(letter, body=None, term=True):
    b=b"\x1b"+letter+b"\n"
    if body: b+=body+b"\n"
    return b+(b"\x80" if term else b"")
def rq(s,q=0.6,t=5):
    s.settimeout(q); buf=b""; t0=time.time()
    while time.time()-t0<t:
        try:
            c=s.recv(65536)
            if not c: break
            buf+=c
        except socket.timeout:
            if buf: break
    return buf
variants = {
 "S FB + D ADF (baseline)": [cmd(b"S",b"FB"), cmd(b"D",b"ADF")],
 "D ADF only":              [cmd(b"D",b"ADF")],
 "S ADF":                   [cmd(b"S",b"ADF")],
 "D ADF + S FB":            [cmd(b"D",b"ADF"), cmd(b"S",b"FB")],
 "S FB + D ADF + I with S=NORMAL_SCAN": [cmd(b"S",b"FB"), cmd(b"D",b"ADF")],
}
for name, seq in variants.items():
    s=socket.create_connection((HOST,54921),timeout=10); rq(s,0.5)
    s.sendall(cmd(b"Q")); rq(s,0.5)
    acks=[]
    for c in seq:
        s.sendall(c); acks.append(rq(s,0.5).hex())
    body=b"R=100,100\nM=CGRAY\nD=SIN" + (b"\nS=NORMAL_SCAN" if "NORMAL" in name else b"")
    s.sendall(cmd(b"I",body)); r=rq(s,0.5)
    print(f"{name:40s} acks={acks} offer={r[3:].decode('ascii','replace').strip(chr(0))}")
    s.close()
