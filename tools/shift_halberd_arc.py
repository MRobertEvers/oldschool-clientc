#!/usr/bin/env python3
"""Slide a player-attached sweep graphic around in the player's local space.

`spotanim_pl` has no lateral offset — the client merges the spot model into the
player's own model and applies a HEIGHT translate and nothing else
(`app_world_sync_one_entity_spotanim`, src/app.c).  So where the arc sits
relative to the player is a property of the model's vertices, and moving it
means moving them.

    python3 tools/shift_halberd_arc.py -- -64          # half a tile to the RIGHT
    python3 tools/shift_halberd_arc.py --dz -90        # 0.7 tiles FORWARD
    python3 tools/shift_halberd_arc.py --model spot/dragon_halberd_special_south_red --show

The shift is RELATIVE and applied every run, so re-tuning is a small step, not
a fresh absolute.  `--show` prints the current bounds without writing.

**Measure before shifting.**  `tools/entity_viewer/ev_swing` reports the offset
this script should be given, split into the part a translation can fix and the
part it cannot — a graphic that is a quarter turn out is not fixable here at
all, and looks from the numbers like a large translation.  See its README.

Sign: a player at yaw 0 faces south (`world_cycle.c` gives yaw 0 to a step with
z decreasing, 1024 to north, 512 to west, 1536 to east), and model space is
world-aligned at yaw 0 — so the model's -Z is the player's forward and its -X
is the player's right.  Negative dx moves the arc to the player's right,
negative dz moves it forward.  128 units is one tile.

The file is an exported asset: `port_lostcity --manifest .../scythe_of_vitur.ini
--apply` rewrites it from cache.osrs239 and undoes whatever this did.  The .ini
carries a note pointing back here; keep the two in step.

Format is ob3 — a 23-byte header at `len - 23` ending in the 0xFFFE marker,
data sections ahead of it.  Only the touched delta streams and their header
byte counts change; every other byte is copied through, so a run with dx=0 and
dz=0 is byte-identical to its input.  That identity is the format check: a
misread of the stream layout does not survive it.
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content/osrs239-content/models")
DEFAULT_MODEL = "spot/dragon_halberd_special_west_red"

# Bit in a vertex's direction flag saying that axis carries a delta.
AXIS_BIT = {"x": 1, "y": 2, "z": 4}
# Order the delta streams appear in, ahead of the header.
AXIS_ORDER = ("x", "y", "z")
# Header offset of each stream's u16 byte count.
AXIS_LEN_AT = {"x": 11, "y": 13, "z": 15}


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
    lengths = {a: u16(head + AXIS_LEN_AT[a]) for a in AXIS_ORDER}
    # One pad byte sits between the Z stream and the header in these records.
    at = {}
    cursor = head - 1 - sum(lengths[a] for a in AXIS_ORDER)
    for a in AXIS_ORDER:
        at[a] = cursor
        cursor += lengths[a]
    return data, head, vertex_count, lengths, at


def decode(data, vertex_count, axis, at, length):
    """The cumulative delta stream for one axis, as a list."""
    deltas, p = [], at
    bit = AXIS_BIT[axis]
    for i in range(vertex_count):
        if data[i] & bit:
            v, p = read_shortsmart(data, p)
            deltas.append(v)
    if p - at != length:
        raise SystemExit(
            "%s stream is %d bytes, header says %d" % (axis.upper(), p - at, length))
    return deltas


def bounds(deltas):
    if not deltas:
        return 0, 0
    vals, acc = [], 0
    for d in deltas:
        acc += d
        vals.append(acc)
    return min(vals), max(vals)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dx", nargs="?", type=int, default=0,
                    help="model units to shift X by; negative is the player's right")
    ap.add_argument("--dz", type=int, default=0,
                    help="model units to shift Z by; negative is forward")
    ap.add_argument("--model", default=DEFAULT_MODEL,
                    help="path under OSRS-Content/osrs239-content/models, without .model "
                         "(default: %s)" % DEFAULT_MODEL)
    ap.add_argument("--show", action="store_true", help="report bounds, write nothing")
    args = ap.parse_args()

    path = os.path.join(CONTENT, args.model + ".model")
    data, head, vertex_count, lengths, at = load(path)
    streams = {a: decode(data, vertex_count, a, at[a], lengths[a]) for a in AXIS_ORDER}

    print("%s: %d vertices" % (args.model, vertex_count))
    for a in AXIS_ORDER:
        lo, hi = bounds(streams[a])
        print("  before: %s %d..%d (centre %+.1f, %+.2f tiles)"
              % (a.upper(), lo, hi, (lo + hi) / 2.0, (lo + hi) / 2.0 / 128.0))
    if args.show or (args.dx == 0 and args.dz == 0):
        return 0

    for axis, delta in (("x", args.dx), ("z", args.dz)):
        if not delta:
            continue
        if not streams[axis]:
            raise SystemExit(
                "%s: no %s deltas in this model, nothing to shift"
                % (args.model, axis.upper()))
        # A cumulative stream: shifting the first entry shifts every vertex.
        streams[axis][0] += delta

    # Rebuild in stream order, so a length change in X moves Y and Z along with
    # it rather than corrupting them.
    out = bytearray(data[: at[AXIS_ORDER[0]]])
    new_lengths = {}
    for a in AXIS_ORDER:
        encoded = b"".join(write_shortsmart(v) for v in streams[a])
        new_lengths[a] = len(encoded)
        out += encoded
    out += data[at[AXIS_ORDER[-1]] + lengths[AXIS_ORDER[-1]]:]

    new_head = len(out) - 23
    for a in AXIS_ORDER:
        out[new_head + AXIS_LEN_AT[a]] = (new_lengths[a] >> 8) & 0xFF
        out[new_head + AXIS_LEN_AT[a] + 1] = new_lengths[a] & 0xFF
    open(path, "wb").write(bytes(out))

    for a in AXIS_ORDER:
        lo, hi = bounds(streams[a])
        print("  after:  %s %d..%d (centre %+.1f, %+.2f tiles)"
              % (a.upper(), lo, hi, (lo + hi) / 2.0, (lo + hi) / 2.0 / 128.0))
    print("  [%d -> %d bytes]  rebuild the cache for this to reach the client"
          % (len(data), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
