#!/usr/bin/env python3
"""The C compositor (ps_compose.c) against the reference (layers.composite).

Both read the SAME stored .pss layer sheets, so this compares the two
compositors and nothing else: every looked-at pixel must agree within a
couple of levels (Python rounds half-to-even, C half-away).

    compose_parity.py <compose_test binary> <store root>
"""

import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image
import io

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "pokesprite"))
import layers  # noqa: E402


def read_pss(path):
    b = open(path, "rb").read()
    assert b[:4] == b"PSS1"
    kind, facings, frames, w, h, ox, oy, gx, gy = struct.unpack_from("<BBHHHhhhh", b, 4)
    off = 4 + struct.calcsize("<BBHHHhhhh")
    durs = list(struct.unpack_from("<%dH" % frames, b, off))
    off += 2 * frames
    (nm,) = struct.unpack_from("<B", b, off)
    off += 1
    table = []
    for _ in range(nm):
        r, g, bl, steps, flags, nh = struct.unpack_from("<6B", b, off)
        off += 6
        hsl = list(struct.unpack_from("<%dH" % nh, b, off))
        off += 2 * nh
        table.append({"base_rgb": [r, g, bl], "steps": steps, "hsl16": hsl,
                      "head": bool(flags & 1), "skin": bool(flags & 2)})
    (n,) = struct.unpack_from("<I", b, off)
    img = np.asarray(Image.open(io.BytesIO(b[off + 4:off + 4 + n])).convert("RGBA"))
    rows = [[img[f * h:(f + 1) * h, i * w:(i + 1) * w] for i in range(frames)] for f in range(facings)]
    return rows, table


def main():
    binary, store = sys.argv[1], sys.argv[2]
    looks = {
        "trainer": (["kit/3", "kit/14", "kit/18", "kit/26", "kit/33", "kit/36", "kit/42"],
                    layers.design_recolour(hair=6, torso=2, legs=4)),
        "rune": (["kit/33", "kit/42", "obj/1163", "obj/1127", "obj/1079", "obj/1201", "obj/1333"],
                 layers.design_recolour(skin=1)),
    }
    names = ["down", "left", "right", "up"]
    worst = 0
    for title, (parts, rec) in looks.items():
        paths = [os.path.join(store, "sheets/layer/m/%s/819.pss" % p) for p in parts]
        sheets = [read_pss(p) for p in paths]
        spec = ",".join("%d:%d" % kv for kv in rec.items()) or "-"
        for facing in range(4):
            for frame in (0, 5):
                with tempfile.NamedTemporaryFile(suffix=".raw") as tf:
                    subprocess.run([binary, tf.name, str(facing), str(frame), spec] + paths,
                                   check=True, capture_output=True)
                    raw = open(tf.name, "rb").read()
                w, h = struct.unpack_from("<ii", raw, 0)
                c = np.frombuffer(raw[8:], np.uint32).reshape(h, w)
                c_rgba = np.stack([(c >> 16) & 255, (c >> 8) & 255, c & 255, c >> 24], -1)
                ref = layers.composite([(rows[facing], table) for rows, table in sheets],
                                       names[facing], frame, rec)
                if not np.array_equal(c_rgba[..., 3] > 0, ref[..., 3] > 0):
                    print("FAIL %s %s f%d: coverage differs at %d px" % (
                        title, names[facing], frame,
                        int(((c_rgba[..., 3] > 0) != (ref[..., 3] > 0)).sum())))
                    sys.exit(1)
                d = np.abs(c_rgba[..., :3].astype(int) - ref[..., :3].astype(int)).max()
                worst = max(worst, int(d))
    print("compositor parity: 16 composites, coverage identical, worst channel diff %d" % worst)
    sys.exit(0 if worst <= 3 else 1)


if __name__ == "__main__":
    main()
