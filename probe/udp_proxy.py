#!/usr/bin/env python3
"""UDP forwarder 127.0.0.1:161 -> HOST:161 (SNMP registration passthrough)."""
import socket, sys
HOST = sys.argv[1]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 161))
up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); up.settimeout(2)
while True:
    d, src = s.recvfrom(65535)
    up.sendto(d, (HOST, 161))
    try:
        r, _ = up.recvfrom(65535); s.sendto(r, src)
    except socket.timeout: pass
