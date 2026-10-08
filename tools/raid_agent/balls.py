#!/usr/bin/env python3
"""Every green-ball landing in a ticklog: who it targeted, where everyone stood.

    python3 tools/raid_agent/balls.py build/raid_agent/verzik/vy00.tsv

Content's rule ([queue,tob_verzik_ball_land]): at a landing exactly ONE
raider who has not had it may be in the 3x3 round the target. Printed per
landing: the tick, the target, each raider's tile and its distance to the
target at the landing tick (the tile the landing reads), and any hit of 50+
that tick (the ball's damage is 75% of the hp level).
"""
import collections
import sys

BALL = "1598"

def unpack(c):
    c = int(c)
    return (c >> 14) & 0x3FFF, c & 0x3FFF

def main(path):
    tiles = collections.defaultdict(dict)
    hits = collections.defaultdict(list)
    launches = []
    for line in open(path):
        c = line.rstrip("\n").split("\t")
        if len(c) < 10 or not c[1].isdigit():
            continue
        tick, kind = int(c[1]), c[2]
        if kind == "player_tile":
            tiles[tick][c[3]] = (int(c[4]), int(c[5]))
        elif kind == "projectile" and c[6] == BALL:
            tgt = int(c[5])
            pid = str(-tgt - 1) if tgt < 0 else (str(tgt - 32768) if tgt >= 32768 else "?")
            launches.append((tick, tick + int(c[8]) // 30, pid))
        elif kind == "hit_player" and int(c[5]) >= 50:
            hits[tick].append("p%s:%s" % (c[3], c[5]))
    for launch, land, pid in launches:
        at = tiles.get(land - 1, {})
        t = at.get(pid)
        cells = []
        for p in sorted(at):
            x, z = at[p]
            d = max(abs(x - t[0]), abs(z - t[1])) if t else -1
            cells.append("p%s%s %d,%d d%d" % (p, "*" if p == pid else "", x - 6400, z - 64, d))
        print("launch t%d land t%d -> p%s | %s | %s" % (launch, land, pid, "  ".join(cells),
              " ".join(hits.get(land, []) + hits.get(land + 1, []))))

if __name__ == "__main__":
    main(sys.argv[1])
