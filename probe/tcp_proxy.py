#!/usr/bin/env python3
"""Logging TCP proxy: listen 127.0.0.1:54921 -> HOST:54921, hexdump both directions."""
import socket, sys, threading, time
HOST = sys.argv[1]; LOG = open(sys.argv[2], "a", buffering=1)
def log(tag, data):
    LOG.write(f"{time.strftime('%H:%M:%S')} {tag} {len(data)}B: {data[:96].hex(' ')}{' ...' if len(data)>96 else ''}\n")
def pump(src, dst, tag):
    try:
        while True:
            d = src.recv(65536)
            if not d: break
            log(tag, d); dst.sendall(d)
    except Exception as e: log(tag+" ERR", str(e).encode())
    finally:
        try: dst.shutdown(socket.SHUT_WR)
        except Exception: pass
def handle(c):
    log("OPEN", b""); p = socket.create_connection((HOST, 54921), timeout=60)
    threading.Thread(target=pump, args=(c, p, "H->P"), daemon=True).start()
    pump(p, c, "P->H"); log("CLOSE", b"")
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 54921)); s.listen(4)
while True:
    c, _ = s.accept(); threading.Thread(target=handle, args=(c,), daemon=True).start()
