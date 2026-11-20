#!/usr/bin/env python3
"""browser_probe.take_frame: a frame is taken only when all of it is in,
however the reads cut it. browser_probe.background_longest: the background
queue's longest step and unit out of a traced client log.

    python3 -m unittest tools/web/test_browser_probe.py
"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from browser_probe import background_longest, take_frame  # noqa: E402


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


class BackgroundLongest(unittest.TestCase):
    def test_the_longest_of_each_and_every_breach(self):
        text = ("queue_contract: trace background longest step 2.10 ms (Dat2MusicDecode)\n"
                "queue_contract: trace background longest unit 0.90 ms (Dat2MusicDecode)\n"
                "noise\n"
                "queue_contract: trace background longest step 16.40 ms (Dat2MusicDecode)\n"
                "queue_contract: trace background longest unit 1.30 ms (WorldStage)\n"
                "queue_contract: background task Lump ran 6.00 ms between two slice checks (its cost clock; "
                "limit 4.00 ms): it overruns any slice by that much\n")
        b = background_longest(text)
        self.assertEqual(b["step"], (16.4, "Dat2MusicDecode"))
        self.assertEqual(b["unit"], (1.3, "WorldStage"))
        self.assertEqual(b["breaches"], [("Lump", 6.0)])

    def test_no_trace(self):
        self.assertEqual(background_longest("nothing"), {"step": None, "unit": None, "breaches": []})


if __name__ == "__main__":
    unittest.main()
