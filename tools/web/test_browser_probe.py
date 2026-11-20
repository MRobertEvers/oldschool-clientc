#!/usr/bin/env python3
"""browser_probe.take_frame: a frame is taken only when all of it is in,
however the reads cut it.

    python3 -m unittest tools/web/test_browser_probe.py
"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from browser_probe import take_frame  # noqa: E402


def frame(payload: bytes) -> bytes:
    if len(payload) < 126:
        head = bytes([0x81, len(payload)])
    elif len(payload) < 65536:
        head = bytes([0x81, 126]) + struct.pack(">H", len(payload))
    else:
        head = bytes([0x81, 127]) + struct.pack(">Q", len(payload))
    return head + payload


class TakeFrame(unittest.TestCase):
    def test_every_cut_of_every_length_form(self):
        for size in (5, 300, 70000):
            payload = bytes((i * 7) & 0xFF for i in range(size))
            data = frame(payload) + frame(b"next")
            whole = len(frame(payload))
            for cut in list(range(0, 12)) + [whole - 1, whole]:
                got, rest = take_frame(data[:cut])
                if cut < whole:
                    self.assertIsNone(got, "size %d cut at %d" % (size, cut))
                    self.assertEqual(rest, data[:cut])
                else:
                    self.assertEqual(got, payload)
            got, rest = take_frame(data)
            self.assertEqual(got, payload)
            self.assertEqual(take_frame(rest), (b"next", b""))


if __name__ == "__main__":
    unittest.main()
