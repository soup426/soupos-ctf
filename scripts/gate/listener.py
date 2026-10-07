#!/usr/bin/env python3
"""A host-side TCP listener for the net segment: echoes upper-cased, or
serves one line of HTTP to a GET. Usage: listener.py <port>."""
import socket, sys
port = int(sys.argv[1])
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port)); s.listen(8); s.settimeout(900)
try:
    while True:
        c, _ = s.accept()
        c.settimeout(5)
        try:
            d = c.recv(1024)
            if d.startswith(b'GET'):
                c.sendall(b'HTTP/1.0 200 OK\r\n\r\nsoup is served\n')
            elif d:
                c.sendall(d.upper())
        except Exception:
            pass
        c.close()
except Exception:
    pass
