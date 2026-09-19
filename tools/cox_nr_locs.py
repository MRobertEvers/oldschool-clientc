#!/usr/bin/env python3
"""Convert Near-Reality (Zenyte) Chambers of Xeric room coordinates into this
tree's room-local (variant, lx, lz) form.

Why this exists
---------------
Every `new Location(x, y, plane)` in a Zenyte CoX room class is an ABSOLUTE tile
on their static template map. `RaidArea.getLocation` turns one into a room-local
tile before use:

    offsetX = x - (staticChunkX * 8)
    offsetY = y - (staticChunkY * 8)

`staticChunkY` is the room's row, published in `map/RaidRoom.java`. `staticChunkX`
is one of 408 / 412 / 416 -- the three layout VARIANTS of that room (Zenyte calls
them by `index`; this tree calls them CCW / THRU / CW). A room is 4 chunks, so a
valid offset lies in 0..31 on both axes.

So the conversion is exact and checkable: for a given room, a coordinate belongs
to whichever of the three chunk-x bases puts it inside the 32x32 box. If none
does -- or more than one does -- the coordinate is not what we think it is, and
the tool says so instead of guessing.

Usage
-----
    tools/cox_nr_locs.py <RoomClass.java> [more.java ...]
    tools/cox_nr_locs.py --room LIZARDMEN_SHAMAN --xz 3273 5270
    tools/cox_nr_locs.py --list

Output is grouped by the Java field the coordinates were declared in, so a
`Location[][] lizardmanSpawnLocations` comes back as one block per variant. A
trailing `*` marks a coordinate that resolves just outside the room's 32x32 box
-- real data (Zenyte's safe polygons reach into the corridor), not an error.
"""

import os
import re
import sys

NR_ROOT = os.path.expanduser(
    "~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main"
    "/core/src/main/java/com/zenyte/game/content/chambersofxeric"
)

# The three static chunk-x bases are the room's layout variants. Ordered as
# Zenyte's `index` 0/1/2, which is west / north / east in getRespectiveTile.
VARIANT_CHUNK_X = (408, 412, 416)
VARIANT_NAME = ("ccw", "thru", "cw")
ROOM_CHUNKS = 4          # a room is 4x4 chunks
ROOM_TILES = ROOM_CHUNKS * 8


def load_rooms():
    """Parse map/RaidRoom.java into {ENUM_NAME: (staticChunkY, height, class)}."""
    path = os.path.join(NR_ROOT, "map", "RaidRoom.java")
    rooms = {}
    pat = re.compile(
        r"^\s*([A-Z][A-Z0-9_]*)\((\d+),\s*(\d+),\s*([A-Za-z]+)\.class"
    )
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = pat.match(line)
            if m:
                rooms[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4))
    return rooms


def rooms_for_class(rooms, cls):
    """Every roster entry served by a given room class."""
    return [(name, y, h) for name, (y, h, c) in rooms.items() if c == cls]


# How far outside the 32x32 box a coordinate may sit and still be reported.
# Zenyte's safe polygons deliberately reach into the corridor south of a room,
# so a small negative lz is real data rather than a mis-assignment.
SLOP = 8


def convert(x, y, chunk_y):
    """Return [(variant, lx, lz, inside)] for every chunk-x base that fits.

    `inside` is False for a coordinate that resolves near the room but outside
    its 32x32 box -- a polygon reaching into the adjoining corridor, say. Those
    are reported rather than dropped, because dropping them silently would make
    a truncated polygon look like a complete one.
    """
    lz = y - chunk_y * 8
    if not -SLOP <= lz < ROOM_TILES + SLOP:
        return []
    hits = []
    for v, chunk_x in enumerate(VARIANT_CHUNK_X):
        lx = x - chunk_x * 8
        if -SLOP <= lx < ROOM_TILES + SLOP:
            inside = 0 <= lx < ROOM_TILES and 0 <= lz < ROOM_TILES
            hits.append((v, lx, lz, inside))
    # A coordinate that lands inside exactly one variant's box is unambiguous;
    # prefer those over slop matches from a neighbouring variant.
    strict = [h for h in hits if h[3]]
    return strict if strict else hits


LOC_RE = re.compile(r"new Location\(\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*(\d+)\s*)?\)")
# `{3290, 5263}` style points, used for the polygons.
PAIR_RE = re.compile(r"\{\s*(\d{4})\s*,\s*(\d{4})\s*\}")
FIELD_RE = re.compile(
    r"(?:private|public|protected|static|final|\s)+"
    r"(?:Location(?:\[\])+|int(?:\[\])+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*="
)


def scan_file(path, rooms):
    cls = os.path.splitext(os.path.basename(path))[0]
    entries = rooms_for_class(rooms, cls)
    if not entries:
        print(f"# {cls}: no RaidRoom entry names this class; "
              f"pass --room to force one", file=sys.stderr)
        return 1
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    print(f"===== {cls}")
    for name, chunk_y, height in sorted(entries):
        print(f"  room {name}  staticChunkY={chunk_y}  height={height}")
    # Every entry for one class shares the class' geometry; if a class serves
    # two roster rows (ResourcesRoom, ScavengerRoom) they differ only in row, so
    # report against each in turn.
    for name, chunk_y, height in sorted(entries):
        print(f"\n  --- as {name} (chunkY {chunk_y}, plane {height})")
        report(text, chunk_y)
    return 0


def report(text, chunk_y):
    # Walk the file once, remembering the most recent field declaration so each
    # coordinate can be attributed to the array it was written in.
    events = []
    for m in FIELD_RE.finditer(text):
        events.append((m.start(), "field", m.group(1)))
    for m in LOC_RE.finditer(text):
        events.append((m.start(), "loc", (int(m.group(1)), int(m.group(2)))))
    for m in PAIR_RE.finditer(text):
        events.append((m.start(), "loc", (int(m.group(1)), int(m.group(2)))))
    events.sort()

    field = "(file scope)"
    out = {}
    order = []
    for _, kind, payload in events:
        if kind == "field":
            field = payload
            continue
        x, y = payload
        hits = convert(x, y, chunk_y)
        if field not in out:
            out[field] = []
            order.append(field)
        out[field].append((x, y, hits))

    if not order:
        print("    (no coordinates)")
        return

    for field in order:
        rows = out[field]
        fitted = [r for r in rows if any(h[3] for h in r[2])]
        print(f"    {field}: {len(rows)} coords, {len(fitted)} inside the room box")
        for x, y, hits in rows:
            if not hits:
                print(f"      ({x},{y}) -- not this room at all; belongs to another row")
                continue
            desc = "  ".join(
                f"{VARIANT_NAME[v]}({v}) lx={lx:3d} lz={lz:3d}" + ("" if ins else "*")
                for v, lx, lz, ins in hits
            )
            flag = "" if len(hits) == 1 else "   <-- AMBIGUOUS, needs the field's index"
            print(f"      ({x},{y}) -> {desc}{flag}")


def main(argv):
    rooms = load_rooms()
    if "--list" in argv:
        for name, (y, h, c) in sorted(rooms.items(), key=lambda kv: (kv[1][0], kv[1][1])):
            print(f"{name:26s} staticChunkY={y:4d} height={h}  {c}")
        return 0
    if "--room" in argv:
        i = argv.index("--room")
        name = argv[i + 1]
        if name not in rooms:
            print(f"no such room: {name}", file=sys.stderr)
            return 2
        chunk_y = rooms[name][0]
        j = argv.index("--xz")
        x, y = int(argv[j + 1]), int(argv[j + 2])
        hits = convert(x, y, chunk_y)
        if not hits:
            print(f"({x},{y}) is not inside {name}")
            return 1
        for v, lx, lz, ins in hits:
            edge = "" if ins else "  (outside the 32x32 box)"
            print(f"({x},{y}) -> {name} variant {VARIANT_NAME[v]}({v}) "
                  f"lx={lx} lz={lz}{edge}")
        return 0

    paths = [a for a in argv[1:] if not a.startswith("-")]
    if not paths:
        print(__doc__)
        return 2
    rc = 0
    for p in paths:
        if not os.path.isabs(p) and not os.path.exists(p):
            p = os.path.join(NR_ROOT, p)
        rc |= scan_file(p, rooms)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
