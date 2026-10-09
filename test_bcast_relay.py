"""Regression checks for the modem-frame loopback relay; no sockets required."""
import unittest

from bcast_relay import ReceiveFraming


class ReceiveFramingTests(unittest.TestCase):
    # Payload includes literal command values and escaped FEND/FESC bytes.
    tx = bytes.fromhex("c0 03 60 03 02 db dc db dd 03 c0")
    rx = bytes.fromhex("c0 02 60 03 02 db dc db dd 03 c0")

    def test_modem_frame_becomes_receive_data_without_changing_payload(self):
        self.assertEqual(ReceiveFraming().feed(self.tx), self.rx)

    def test_every_tcp_split_including_command_and_escape_boundaries(self):
        for split in range(len(self.tx) + 1):
            with self.subTest(split=split):
                framing = ReceiveFraming()
                actual = framing.feed(self.tx[:split]) + framing.feed(self.tx[split:])
                self.assertEqual(actual, self.rx)

    def test_byte_at_a_time(self):
        framing = ReceiveFraming()
        actual = b"".join(framing.feed(bytes([byte])) for byte in self.tx)
        self.assertEqual(actual, self.rx)

    def test_multiple_frames_and_repeated_delimiters(self):
        tx = self.tx + b"\xc0\xc0" + self.tx
        rx = self.rx + b"\xc0\xc0" + self.rx
        self.assertEqual(ReceiveFraming().feed(tx), rx)

    def test_preserves_port_nibble_and_other_commands(self):
        tx = bytes.fromhex("c0 53 03 c0 c0 02 03 c0 c0 06 03 c0")
        rx = bytes.fromhex("c0 52 03 c0 c0 02 03 c0 c0 06 03 c0")
        self.assertEqual(ReceiveFraming().feed(tx), rx)

    def test_connections_have_independent_state(self):
        first, second = ReceiveFraming(), ReceiveFraming()
        self.assertEqual(first.feed(b"\xc0"), b"\xc0")
        self.assertEqual(second.feed(self.tx[:4]), self.rx[:4])
        self.assertEqual(first.feed(self.tx[1:]), self.rx[1:])
        self.assertEqual(second.feed(self.tx[4:]), self.rx[4:])


if __name__ == "__main__":
    unittest.main()
