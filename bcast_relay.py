#!/usr/bin/env python3
"""Loopback relay for modem frames (see bcast_loopback_test.sh).

Mercury accepts CMD_MODEM_FRAME on transmit and delivers the received modem
frame as CMD_DATA. Model that command change without touching the escaped
payload. This is not a simulator for Mercury's other command types or RF path.
"""
import socket
import sys
import threading

FEND = 0xC0
CMD_DATA = 0x02
CMD_MODEM_FRAME = 0x03


class ReceiveFraming:
    """Translate KISS command bytes, retaining state across TCP reads."""

    def __init__(self):
        self.awaiting_command = False

    def feed(self, data):
        out = bytearray(data)
        for i, byte in enumerate(data):
            if byte == FEND:
                self.awaiting_command = True
            elif self.awaiting_command:
                if byte & 0x0F == CMD_MODEM_FRAME:
                    out[i] = (byte & 0xF0) | CMD_DATA
                self.awaiting_command = False
        return bytes(out)


def pump(src, dsts):
    framing = ReceiveFraming()
    while True:
        try:
            data = src.recv(65536)
        except OSError:
            break
        if not data:
            break
        data = framing.feed(data)
        for d in list(dsts):
            if d is src:
                continue
            try:
                d.sendall(data)
            except OSError:
                pass


def main():
    port = int(sys.argv[1])
    conns = []
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(4)
        while True:
            c, _ = srv.accept()
            conns.append(c)
            threading.Thread(target=pump, args=(c, conns), daemon=True).start()


if __name__ == "__main__":
    main()
