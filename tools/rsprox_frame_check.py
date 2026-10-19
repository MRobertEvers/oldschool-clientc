#!/usr/bin/env python3
"""Which parts of an RSProx recording a TRANSLATED transcript measures right.

RSProx's transcriber maps every instance tile back to the template tile it was
copied from (TRANSLATE_INSTANCED_COORDS, on by default). In an unturned or
180-degree room that keeps distances. In a room turned a QUARTER it mirrors
each tile's offset inside its 8x8 chunk, so a distance across a chunk edge
comes out wrong -- for a player as much as an npc. Vasa's teleport and boulder
range were misread that way (docs/rsprox-analysis/cox/vasa.md RV-S2, RV-B3).

Give it the same recording transcribed twice, translated and untranslated
(docs/rsprox-analysis/cox/README.md, "Instance tiles"). For every pair of 1x1
tiles printed on one tick within 20 of each other, it compares the true
(untranslated) distance with the translated one, per 32x32 template room
cell (both tiles in it). A cell with wrong pairs needs
tools/cox/rsprox_room.py's correction or an untranslated transcript; a cell
with none can be measured as printed (npc
tiles aside: a size-N npc's printed tile is another corner of its footprint in
any turned room).

    python3 tools/rsprox_frame_check.py <translated>/3285.txt <raw>/3285.txt
"""
import argparse
import re
from collections import defaultdict

COORD = re.compile(r"(?<![a-z])(coord|newcoord)=\((\d+), (\d+), (\d+)\)")
TICK = re.compile(r"^\[(\d+)\]")
INSTANCE_X = 6400
NEAR = 20


def check(translated, raw):
    stats = defaultdict(lambda: [0, 0])
    tick_tiles = set()

    def flush():
        pts = list(tick_tiles)
        for i in range(len(pts)):
            ti, ri = pts[i]
            for j in range(i + 1, len(pts)):
                tj, rj = pts[j]
                if ri[2] != rj[2]:
                    continue
                dr = max(abs(ri[0] - rj[0]), abs(ri[1] - rj[1]))
                if dr == 0 or dr > NEAR:
                    continue
                key = (ti[0] // 32 * 32, ti[1] // 32 * 32, ti[2])
                if key != (tj[0] // 32 * 32, tj[1] // 32 * 32, tj[2]):
                    continue        # two template rooms, maybe turned apart
                dt = max(abs(ti[0] - tj[0]), abs(ti[1] - tj[1]))
                stats[key][0] += 1
                if dt != dr:
                    stats[key][1] += 1

    with open(translated, errors="replace") as ft, open(raw, errors="replace") as fr:
        for lt, lr in zip(ft, fr):
            if TICK.match(lt):
                flush()
                tick_tiles = set()
                continue
            if lt == lr or "npc=(" in lt:
                continue
            ct, cr = COORD.findall(lt), COORD.findall(lr)
            if len(ct) != len(cr):
                continue
            for a, b in zip(ct, cr):
                t = (int(a[1]), int(a[2]), int(a[3]))
                r = (int(b[1]), int(b[2]), int(b[3]))
                if r[0] >= INSTANCE_X and t[0] < INSTANCE_X:
                    tick_tiles.add((t, r))
    flush()
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("translated")
    ap.add_argument("raw")
    ap.add_argument("--min-pairs", type=int, default=20)
    args = ap.parse_args()
    for key, (total, wrong) in sorted(check(args.translated, args.raw).items()):
        if total < args.min_pairs:
            continue
        verdict = "ok" if wrong == 0 else "WRONG DISTANCES"
        print("template room (%d, %d) level %d: %d pairs, %d wrong -- %s" % (key + (total, wrong, verdict)))


if __name__ == "__main__":
    main()
