#!/usr/bin/env python3
"""Where a dying jelly's blood goes: per jelly death (its headbar at 0 on its
tile), the blood tiles added that tick that no slam, landing or heavy shot
explains, against the death tiles and the blood already down.

    python3 dn_jellyblood.py <all-players txt>... [--rows]
"""
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P

JEL = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=tfa_jelly_minion \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")


def deaths(path):
    out = defaultdict(list)   # tick -> [(start tile, end tile)]
    tick = None
    cur = None
    seen = set()
    for line in open(path, errors="replace"):
        m = P.TICK.match(line.rstrip("\n"))
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        m = JEL.match(line)
        if m:
            a = (int(m.group(3)), int(m.group(4)))
            b = (int(m.group(5)), int(m.group(6))) if m.group(5) else a
            cur = (m.group(2), a, b)
            continue
        if line.startswith("        [") or line.startswith("    ["):
            cur = None
        if cur and "[headbar] id=0, fill=0" in line and cur[0] not in seen:
            seen.add(cur[0])
            out[tick].append((cur[1], cur[2]))
    return out


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    rows = "--rows" in sys.argv
    st = Counter()
    offs = Counter()
    per = Counter()
    for path in paths:
        s = P.parse(path)
        dd = deaths(path)
        tag = path[-8:-4]
        slam, land, p2 = {}, {}, defaultdict(set)
        for t in s.ticks:
            if t in s.boss and "dreadnoth_jump01" in s.boss[t][2]:
                slam[t + 1] = s.boss[t][1]
            if t in s.boss and "dreadnoth_jump02" in s.boss[t][2]:
                land[t + 3] = s.boss[t][1]
        for t, tiles in s.proj2.items():
            for x in tiles:
                p2[t + 6].add(x)
        blood = set()
        for t in s.ticks:
            if t in s.boss_adds:
                blood = set()
            adds = set(s.blood_add.get(t, ()))
            other = set()
            for x in adds:
                for d in (slam, land):
                    if t in d and d[t][0] - 1 <= x[0] <= d[t][0] + 5 and d[t][1] - 1 <= x[1] <= d[t][1] + 5:
                        other.add(x)
                if x in p2.get(t, ()):
                    other.add(x)
            mine = adds - other
            ds = dd.get(t, [])
            if ds or mine:
                per[(len(ds), len(mine))] += 1
                tiles = [b for a, b in ds]
                free_own = sum(1 for x in set(tiles) if x not in blood)
                for x in mine:
                    near = min(tiles, key=lambda y: max(abs(x[0] - y[0]), abs(x[1] - y[1]))) if tiles else None
                    if near:
                        offs[(x[0] - near[0], x[1] - near[1])] += 1
                for x in set(tiles):
                    st["death tile already blood" if x in blood else "death tile fresh"] += 1
                    st[("own tile got blood", x in mine, "was blood", x in blood)] += 1
                if rows:
                    print(tag, t, "deaths at", tiles, "blood", sorted(mine), "already blood", sorted(set(tiles) & blood))
            blood |= adds
            blood = {x for x in blood if not (x in s.blood_gone and s.blood_gone[x] <= t)}
    print("(deaths, unexplained blood) per tick:", sorted(per.items()))
    print("blood offset from the nearest death tile:", sorted(offs.items(), key=lambda kv: -kv[1])[:30])
    for k, v in sorted(st.items(), key=str):
        print(" ", k, v)


if __name__ == "__main__":
    main()
