#!/usr/bin/env python3
"""
Ground-over-entity check over a TORIRS_WEDGELOG capture.

THE INVARIANT
-------------
A tile's GROUND -- its terrain meshes, its ground decor, its ground objects --
belongs under everything that stands on that tile. The painter enforces it with
the span gate: a scenery element is emitted only once every tile of its
footprint has had its ground pass. So for every element the log shows being
drawn, nothing that is part of a tile inside that element's footprint may be
drawn after it.

A TILE IS (plane, x, z), NEVER (x, z). The grid is a stack: a bridge deck, an
upper storey and the square under it share one x,z and are three different
tiles, drawn lowest plane first. Reading the footprint against x,z alone makes
every upper-storey floor in Lumbridge a violation against the loc on the ground
floor beneath it -- 30,000 of them in a thousand frames, all of them correct
draws. A gate that cries wolf that loudly is worse than no gate, which is why
this is written down rather than left as a column the parser happens to keep.

This reads the log and says so. It is a DRAW ORDER check and not a picture
check, which is the point: the defect it was written for (the Inferno's lava
plates landing on top of the Ancestral Glyph) is a handful of frames long, is
invisible from most camera angles even while it is happening, and moves with
the encounter. A screenshot can miss it in a way the command stream cannot.

WHAT IT NEEDS
-------------
A log written by a PAINTERS_DEBUG=1 build, which is what puts the `fp=` column
-- an element's own anchor and footprint -- on every geometry row. Without it
the row names only the tile the walk happened to emit from, and for a multi-tile
element that is whichever footprint tile popped last, so the footprint cannot be
recovered and this check cannot be written at all.

    make -C src PAINTERS_DEBUG=1 ...
    TORIRS_WEDGELOG=<path> TORIRS_WEDGELOG_AT=<n> TORIRS_WEDGELOG_FRAMES=<n> ...
    python3 tools/zuk_glyph/check_draw_order.py <path>

Exit status is 1 when anything was found, so it drops straight into a gate.
"""
import argparse
import re
import sys

# Everything a tile's own GROUND PASS puts down -- bucket_emit_terrain plus
# bucket_emit_tile_features. Exactly this set is what the span gate promises is
# already down when an element covering the tile is emitted, and exactly this
# set is what the seam exception defers, so these are the kinds that can land on
# an entity.
GROUND = {
    "floor",
    "floor:bridge",
    "grounddecor",
    "item",
    "wall_a",
    "wall_b",
    "decor",
    "decor_alt",
}
# Deliberately NOT in it: `wall_back_a`, `wall_back_b`, `decor_back`,
# `decor_back_alt` and `item_back` are the NEAR set, emitted at tile completion
# *after* the tile's scenery on purpose -- a near wall belongs in front of what
# stands behind it. Nor `loc`: a loc is scenery and sorts against other scenery
# within the tile, which is a different question with a different right answer.
#
# Rows that carry a footprint worth protecting.
DRAWN = {"entity", "loc"}

ROW = re.compile(
    r"^(?P<seq>\d+) (?P<plane>\d+) (?P<x>-?\d+) (?P<z>-?\d+) \S+ \S+ (?P<what>\w+)"
    r"(?: p=\d+ ent=(?P<ent>-?\d+) elem=(?P<elem>-?\d+) spans=\S+ flags=\S+)?"
    r"(?: fp=(?P<fx>-?\d+),(?P<fz>-?\d+)(?:\+(?P<fw>\d+)x(?P<fh>\d+))?)?"
)


def frames(path):
    """Yield (frame_number, [row match dicts]) per `#frame` block."""
    number, rows = None, []
    with open(path) as handle:
        for line in handle:
            if line.startswith("#frame"):
                if number is not None:
                    yield number, rows
                number, rows = int(line.split()[1]), []
            elif line.startswith("#"):
                continue
            elif number is not None:
                match = ROW.match(line.rstrip("\n"))
                if match:
                    rows.append(match.groupdict())
    if number is not None:
        yield number, rows


def violations(path, only_entity=None):
    for number, rows in frames(path):
        # Elements drawn so far this frame, per plane, with the footprint each
        # one claimed. An element's footprint is a span on ITS OWN plane.
        standing = {}
        for row in rows:
            what = row["what"]
            plane = int(row["plane"])
            if what in DRAWN and row["fx"] is not None and row["fw"] is not None:
                x0, z0 = int(row["fx"]), int(row["fz"])
                standing.setdefault(plane, []).append(
                    (
                        int(row["seq"]),
                        row["ent"],
                        what,
                        x0,
                        z0,
                        x0 + int(row["fw"]) - 1,
                        z0 + int(row["fh"]) - 1,
                    )
                )
            elif what in GROUND:
                tile_x, tile_z = int(row["x"]), int(row["z"])
                for seq, ent, kind, x0, z0, x1, z1 in standing.get(plane, ()):
                    if x0 <= tile_x <= x1 and z0 <= tile_z <= z1:
                        if only_entity is not None and ent != only_entity:
                            continue
                        yield {
                            "frame": number,
                            "what": what,
                            "plane": plane,
                            "tile": (tile_x, tile_z),
                            "ground_seq": int(row["seq"]),
                            "ent": ent,
                            "kind": kind,
                            "footprint": (x0, z0, x1, z1),
                            "drawn_seq": seq,
                        }


def main():
    # A report of thousands of rows is normally read through `head`, and a
    # SIGPIPE traceback on the way out reads as the checker having crashed.
    try:
        import signal

        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (ImportError, AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="a TORIRS_WEDGELOG capture")
    parser.add_argument(
        "--entity",
        help="report only this scene entity id (the wedge log's ent= column)",
    )
    parser.add_argument(
        "--quiet", action="store_true", help="print the count and nothing else"
    )
    args = parser.parse_args()

    found = list(violations(args.log, args.entity))
    if not args.quiet:
        for bad in found:
            print(
                "frame {frame}: {what} of tile plane {plane} {tile} drawn at seq "
                "{ground_seq}, ON TOP OF {kind} ent={ent} (footprint {footprint}) "
                "drawn at seq {drawn_seq}".format(**bad)
            )
    print("%d ground-over-entity violation(s)" % len(found))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
