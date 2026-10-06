#!/usr/bin/env python3
"""goto_table.py <ledger.tsv> [--allow-op-locs] [--root <repo>] -- every goto row: departure, landing, static-collision reachability.

Each same-level hop of at most 1,200 tiles on its longer axis (helper_coverage's GATE_MAX_TILES: the
grader floods those too) goes through reach.py (doors closed, op locs and zone-trigger tiles
blocked: see its banner), at margin 30, then 100, then 250 until it reads REACH -- a route that
leaves the 30-tile box is still a route, and a charge is the widest box's. NEEDS-DOOR or NEEDS-OP
charges the hop: the walk must click what it names (a blocking crossing loc -- the Wilderness Ditch, the
Shantay Pass -- is NEEDS-OP since seam matthew-mbp-m4-b64-seam1). --allow-op-locs passes through (the
old flood that walks over traps). --root reads another checkout's maps (default: the one this file is in).
Before that seam a hop over 400 tiles printed "far ... not checked", which fixers read as "not judged";
the grader judged it all along."""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import reach  # noqa: E402

MARGINS = (30, 100, 250)
FAR_TILES = 1200   # helper_coverage.Grader.GATE_MAX_TILES


def verdict(sx, sz, tx, tz, level, allow_op_locs=False):
    """REACH at the first margin that finds it; else the widest margin's charge (a wider box can only
    find a route with fewer doors or ops); else UNREACHABLE at the widest."""
    found = None
    for margin in MARGINS:
        r = reach.answer(sx, sz, tx, tz, level, margin, allow_op_locs)
        if r.startswith("REACH"):
            return r
        if not r.startswith("UNREACHABLE"):
            found = r
    return found or r


def rows(path, allow_op_locs=False):
    """[(index, step, verdict word, text)] for every goto row of the ledger."""
    out = []
    for line in open(path):
        c = line.rstrip("\n").split("\t")
        if len(c) < 6 or "goto" not in c[1]:
            continue
        m = re.search(r"at (\d+),(\d+),(\d+) from (\d+),(\d+),(\d+)", c[5])
        if not m:
            out.append((c[0], c[1], c[2], "no stamp: %s" % c[5][:120]))
            continue
        tx, tz, tl, sx, sz, sl = map(int, m.groups())
        if (sx, sz) == (tx, tz):
            r = "same tile"
        elif tl != sl:
            r = "LEVEL CHANGE %d->%d" % (sl, tl)
        elif max(abs(sx - tx), abs(sz - tz)) > FAR_TILES:
            r = "far (%d tiles on the longer axis): over helper_coverage's GATE_MAX_TILES, flooded by " \
                "neither" % max(abs(sx - tx), abs(sz - tz))
        else:
            r = verdict(sx, sz, tx, tz, tl, allow_op_locs)
        out.append((c[0], c[1], c[2], "%d,%d,%d <- %d,%d,%d|%s" % (tx, tz, tl, sx, sz, sl, r)))
    return out


if __name__ == "__main__":
    argv = reach.take_root(sys.argv[1:])
    allow = "--allow-op-locs" in argv
    args = [a for a in argv if a != "--allow-op-locs"]
    if not args:
        sys.exit(__doc__)
    for row in rows(args[0], allow):
        print("|".join(row))
