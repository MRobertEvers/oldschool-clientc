#!/usr/bin/env python3
"""
Summarise a TORIRS_MOVER_FOOTPRINT_DEBUG trace: does each mover's painter span
cover the model that is drawn in it?

An entity registers with the painter over the tile span its DECLARED SIZE
claims -- `(size - 1) * 64 + 60` fine units of padding around its draw position,
reference `World.addDynamic` -- and the model it draws is under no obligation to
fit inside that. Where it does not, the tiles it pokes into are tiles the span
gate never waited for, and the ones on the camera side of the entity put their
floor down after it. That is a second, independent way for the floor to land on
an npc, and it looks exactly like the first one on screen.

So this is the question a "the floor is on top of X" report gets asked first,
and the raw trace is one line per mover per cycle -- tens of thousands of them.
This reduces it to one row per element: the worst overhang it ever had.

    tools/zuk_glyph/run.sh spans        # runs it for you
    TORIRS_MOVER_FOOTPRINT_DEBUG=1 <client> 2>trace.txt
    python3 tools/zuk_glyph/summarise_spans.py trace.txt

READING IT

`radius` is the bounding cylinder's CORNER diagonal, so a model that fills its
tiles exactly reports `size * 64 * sqrt(2)` and reads as a model overhanging by
41% when it does not overhang at all. It is in the trace because it is what the
renderer culls against, and in this report only so nobody reasons from it by
mistake. `reach` is the axis-aligned pair, and `reach - pad` is the overhang.

A small positive overhang is EXPECTED and is the reference's own behaviour: a
size-1 player pads 60 against a model reaching ~79, so a fifth of a tile of
elbow does stand on the neighbouring square. Widening the span to cover it is
not a fix -- the span is also what orders an entity against other entities, so
inflating it makes an npc claim tiles nearer the camera than the projectile
flying at it. @see docs/glyph_lava_overdraw.md. What this report is for is
telling a span that is short by a FIFTH of a tile from one that is short by
two tiles, because only the second can explain a floor plate across a boss.

WHY THERE ARE TWO OVERHANG COLUMNS

The worst overhang an element ever had is not what it usually has, and for
several elements the worst is ONE CYCLE. The bounds an element carries are the
POSED model's, refreshed when a pose is applied -- so on the cycle a model is
first assigned and before its first pose lands, they still describe the BIND
pose. TzKal-Zuk reaches 674 on that one cycle against 473 for the rest of the
fight; the Ancestral Glyph reaches 494 against ~220. Reported as a maximum
alone, both read as models two tiles too big for their span, and the next
person to look spends an afternoon on it. `cycles>tile` is how many cycles the
overhang actually exceeded a tile. A handful out of thousands is model swaps --
TzKal-Zuk scores two because he is assigned a model twice, once to emerge and
once to stand. A sustained count is a real overhang and worth chasing.
"""
import argparse
import collections
import re
import sys

ROW = re.compile(
    r"mover footprint: el=(?P<el>-?\d+) at (?P<x>-?\d+),(?P<z>-?\d+) "
    r"pad=(?P<pad>\d+) radius=(?P<radius>\d+) reach=(?P<rx>\d+),(?P<rz>\d+) "
    r"span=(?P<sx>-?\d+),(?P<sz>-?\d+)\+(?P<w>\d+)x(?P<h>\d+)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="stderr of a TORIRS_MOVER_FOOTPRINT_DEBUG run")
    parser.add_argument(
        "--min-overhang",
        type=int,
        default=0,
        help="only report elements whose worst overhang is at least this many "
        "fine units (128 = one tile)",
    )
    args = parser.parse_args()

    worst = collections.OrderedDict()
    for line in open(args.trace):
        match = ROW.search(line)
        if not match:
            continue
        element = int(match.group("el"))
        pad = int(match.group("pad"))
        reach_x, reach_z = int(match.group("rx")), int(match.group("rz"))
        radius = int(match.group("radius"))
        row = worst.setdefault(
            element, {"pad": pad, "rx": 0, "rz": 0, "radius": 0, "cycles": 0,
                      "over_tile": 0, "overhangs": []}
        )
        row["pad"] = pad
        row["rx"] = max(row["rx"], reach_x)
        row["rz"] = max(row["rz"], reach_z)
        row["radius"] = max(row["radius"], radius)
        row["cycles"] += 1
        overhang = max(reach_x, reach_z) - pad
        row["overhangs"].append(overhang)
        if overhang >= 128:
            row["over_tile"] += 1

    if not worst:
        print(
            "no `mover footprint:` rows -- was TORIRS_MOVER_FOOTPRINT_DEBUG=1 set?",
            file=sys.stderr,
        )
        return 2

    print(
        "%-12s %7s %5s %8s %8s %8s %11s %7s"
        % (
            "element",
            "cycles",
            "pad",
            "reach x",
            "reach z",
            "worst",
            "cycles>tile",
            "radius",
        )
    )
    flagged = 0
    for element, row in sorted(worst.items()):
        overhangs = sorted(row["overhangs"])
        worst_overhang = overhangs[-1]
        # The overhang it actually lives at, not the one frame it spiked to.
        typical = overhangs[len(overhangs) // 2]
        if worst_overhang < args.min_overhang:
            continue
        flagged += 1
        # A few cycles out of thousands is a model swap showing its bind pose,
        # not a model that does not fit. The rate is what separates them.
        swap_transient = row["over_tile"] <= max(2, row["cycles"] // 100)
        if row["over_tile"] and not swap_transient:
            note = "  <-- a WHOLE TILE outside its span, on %d of %d cycles" % (
                row["over_tile"],
                row["cycles"],
            )
        elif row["over_tile"]:
            note = "  (%d cycle(s): a model swap's bind pose, before the first pose)" % (
                row["over_tile"],
            )
        elif typical > 0:
            note = "  (expected: the reference pads by size, not by model)"
        else:
            note = ""
        print(
            "%-12d %7d %5d %8d %8d %8d %11d %7d%s"
            % (
                element,
                row["cycles"],
                row["pad"],
                row["rx"],
                row["rz"],
                worst_overhang,
                row["over_tile"],
                row["radius"],
                note,
            )
        )
    print("%d element(s) reported" % flagged)
    return 0


if __name__ == "__main__":
    sys.exit(main())
