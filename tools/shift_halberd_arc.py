#!/usr/bin/env python3
"""Slide the red sweep arc sideways in the player's local space.

`spotanim_pl` has no lateral offset — the client merges the spot model into the
player's own model and applies a HEIGHT translate and nothing else
(`app_world_sync_one_entity_spotanim`, src/app.c).  So where the arc sits
relative to the player is a property of the model's vertices, and moving it
means moving them.

    python3 tools/shift_halberd_arc.py -64     # half a tile to the player's RIGHT
    python3 tools/shift_halberd_arc.py 64      # and back again

The shift is RELATIVE and applied every run, so re-tuning is a small step, not
a fresh absolute.  `--show` prints the current bounds without writing.

Sign: a player at yaw 0 faces south (`world_cycle.c` gives yaw 0 to a step with
z decreasing, 1024 to north, 512 to west, 1536 to east), and model space is
world-aligned at yaw 0 — so the model's -Z is the player's forward and its -X
is the player's right.  Negative dx moves the arc to the player's right.

The file is an exported asset: `port_lostcity --manifest .../scythe_of_vitur.ini
--apply` rewrites it from cache.osrs239 and undoes whatever this did.  The .ini
carries a note pointing back here; keep the two in step.

Format is ob3 — a 23-byte header at `len - 23` ending in the 0xFFFE marker,
data sections ahead of it.  Only the X delta stream and the header's
xDataByteCount are touched; every other byte is copied through, so a run with
dx=0 is byte-identical to its input.
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL = os.path.join(
    REPO,
    "OSRS-Content/osrs239-content/models/spot/dragon_halberd_special_west_red.model",
)


def read_shortsmart(buf, p):
    v = buf[p]
    if v < 128:
        return v - 64, p + 1
    return ((buf[p] << 8) | buf[p + 1]) - 49152, p + 2


def write_shortsmart(v):
    if -64 <= v <= 63:
        return bytes([v + 64])
    if not -16384 <= v <= 16383:
        raise ValueError("delta %d does not fit a shortsmart" % v)
    w = v + 49152
    return bytes([(w >> 8) & 0xFF, w & 0xFF])


def load(path):
    data = bytearray(open(path, "rb").read())
    head = len(data) - 23
    if (data[len(data) - 2], data[len(data) - 1]) != (0xFF, 0xFE):
        raise SystemExit("%s: not an ob3 model (no 0xFFFE marker)" % path)

    def u16(o):
        return (data[o] << 8) | data[o + 1]

    vertex_count = u16(head)
    x_len, y_len, z_len = u16(head + 11), u16(head + 13), u16(head + 15)
    # One pad byte sits between the Z stream and the header in these records.
    x_at = head - 1 - z_len - y_len - x_len
    return data, head, vertex_count, x_len, x_at


def decode_x(data, vertex_count, x_at, x_len):
    deltas, p = [], x_at
    for i in range(vertex_count):
        if data[i] & 1:  # bit 0 of the vertex direction flag: an X delta follows
            v, p = read_shortsmart(data, p)
            deltas.append(v)
    if p - x_at != x_len:
        raise SystemExit("X stream is %d bytes, header says %d" % (p - x_at, x_len))
    return deltas


def bounds(deltas):
    xs, acc = [], 0
    for d in deltas:
        acc += d
        xs.append(acc)
    return min(xs), max(xs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dx", nargs="?", type=int, default=0,
                    help="model units to shift X by; negative is the player's right "
                         "(128 units = one tile)")
    ap.add_argument("--show", action="store_true", help="report bounds, write nothing")
    args = ap.parse_args()

    data, head, vertex_count, x_len, x_at = load(MODEL)
    deltas = decode_x(data, vertex_count, x_at, x_len)
    lo, hi = bounds(deltas)
    print("before: X %d..%d (centre %+.1f, %+.2f tiles)"
          % (lo, hi, (lo + hi) / 2.0, (lo + hi) / 2.0 / 128.0))
    if args.show or args.dx == 0:
        return 0

    deltas[0] += args.dx  # cumulative stream: shifting the first shifts them all
    stream = b"".join(write_shortsmart(v) for v in deltas)
    out = bytearray(bytes(data[:x_at]) + stream + bytes(data[x_at + x_len:]))
    new_head = head + (len(stream) - x_len)
    out[new_head + 11] = (len(stream) >> 8) & 0xFF
    out[new_head + 12] = len(stream) & 0xFF
    open(MODEL, "wb").write(bytes(out))

    lo, hi = bounds(deltas)
    print("after:  X %d..%d (centre %+.1f, %+.2f tiles)   [%d -> %d bytes]"
          % (lo, hi, (lo + hi) / 2.0, (lo + hi) / 2.0 / 128.0, len(data), len(out)))
    print("rebuild the cache for this to reach the client")
    return 0


if __name__ == "__main__":
    sys.exit(main())
