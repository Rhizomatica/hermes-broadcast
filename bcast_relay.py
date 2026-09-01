#!/usr/bin/env python3
"""Two-client byte relay standing in for the HF path (see bcast_loopback_test.sh)."""
import socket, sys, threading
port = int(sys.argv[1])
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port)); srv.listen(4)
conns = []
def pump(src, dsts):
    while True:
        try: b = src.recv(65536)
        except OSError: break
        if not b: break
        for d in list(dsts):
            if d is src: continue
            try: d.sendall(b)
            except OSError: pass
while True:
    c, _ = srv.accept()
    conns.append(c)
    threading.Thread(target=pump, args=(c, conns), daemon=True).start()
