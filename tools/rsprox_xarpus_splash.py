#!/usr/bin/env python3
"""Xarpus's spit splashes against EVERY raider's tile at the lock.

Needs transcripts made with PLAYER_INFO_LOCAL_PLAYER_ONLY=false (every raider
shown; recipe in docs/rsprox-analysis/cox/README.md, "Instance tiles" -- the
ToB rooms translate exactly, so translation may stay on). The archive's own
transcripts show the recorder alone, which cannot tell where the other
raiders stood.

For each spit (tob_xarpus_acidspit 1555, start 30) it takes every raider's
tile at the end of S - 2 (the lock, xarpus.md) inside the arena, and every
splash thrown from that spit's pool (1555, start 15), and prints:
  * landings by party size, raiders on the spit's tile, the other raiders'
    distinct tiles, and where the splashes went (the spit's tile, another
    raider's tile, or a random tile, pooled or fresh);
  * the random splashes: how many fell on a pool, against the pooled share of
    the arena then, and whether they fell inside the 15x15 arena.

    python3 tools/rsprox_xarpus_splash.py <allp>/archive/{3299,3285,788,1833,1764,553,1779,1837,582}.txt
"""
import re, sys
from collections import Counter, defaultdict

TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^\s{8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
XAR = re.compile(r'npc=\(index=(\d+), (?:id=[a-z0-9_]+ \((83[3-4]\d)\)|[^()]*\(id=(83[3-4]\d)\)), coord=\((\d+), (\d+), (\d+)\)')
PROJ = re.compile(r'^\s{4}\[map_projanim_v\d\] id=(?:[a-z0-9_]+ \()?(\d+)\)?, starttime=(\d+), endtime=(\d+)')
SRC = re.compile(r'^\s{8}\[source\] coord=\((\d+), (\d+), (\d+)\)')
TGT = re.compile(r'^\s{8}\[target\] coord=\((\d+), (\d+), (\d+)\)')
POOL = re.compile(r'^\s{4}\[loc_add_change_v\d\] id=(?:[a-z0-9_]+ \()?32744\)?, coord=\((\d+), (\d+), (\d+)\)')
SPIT, SPLASH_START, SPIT_START = 1555, 15, 30
XIDS = {8338, 8339, 8340, 8341}


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def read(path):
    tick = 0
    end = {}                # tick -> {name: tile} at the end of the tick
    pos = {}
    xar = None              # his sw
    projs = []              # (tick, id, start, end, src, tgt)
    pools = {}              # tile -> first tick
    cur = None
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            end[tick] = dict(pos)
            tick = int(m.group(1))
            cur = None
            continue
        m = PL.match(line)
        if m:
            name = m.group(3)
            if m.group(1) == 'remove':
                pos.pop(name, None)
                continue
            pos[name] = (int(m.group(7)), int(m.group(8)), int(m.group(9))) if m.group(7) else \
                        (int(m.group(4)), int(m.group(5)), int(m.group(6)))
            continue
        m = XAR.search(line)
        if m and int(m.group(2) or m.group(3)) in XIDS and xar is None:
            xar = (int(m.group(4)), int(m.group(5)), int(m.group(6)))
        m = PROJ.match(line)
        if m:
            cur = [tick, int(m.group(1)), int(m.group(2)), int(m.group(3)), None, None]
            projs.append(cur)
            continue
        if cur:
            m = SRC.match(line)
            if m:
                cur[4] = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
                continue
            m = TGT.match(line)
            if m:
                cur[5] = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
                cur = None
                continue
        m = POOL.match(line)
        if m:
            pools.setdefault((int(m.group(1)), int(m.group(2)), int(m.group(3))), tick)
    end[tick] = dict(pos)
    return end, xar, projs, pools



ARENA_ORIGIN = (3136 + 27, 4352 + 28)   # tob.constant ^tob_xarpus_area_lx/lz, template room (3136, 4352)
ARENA = 15


def main():
    shapes = Counter()
    random_n = random_pooled = 0
    pooled_share = []
    inside = 0
    for path in sys.argv[1:]:
        end, xar, projs, pools = read(path)
        if xar is None:
            continue
        centre = (xar[0] + 2, xar[1] + 2, xar[2])
        spits = [p for p in projs if p[1] == SPIT and p[2] == SPIT_START and p[5]]
        splashes = [p for p in projs if p[1] == SPIT and p[2] == SPLASH_START and p[5]]
        for s, _, _, e, src, tgt in spits:
            land = s + e // 30
            lock = end.get(s - 2, {})
            raiders = {n: t for n, t in lock.items() if t[2] == centre[2] and cheb(t, centre) <= 9}
            if not raiders:
                continue
            on_t = sum(1 for t in raiders.values() if t == tgt)
            others = set(t for t in raiders.values() if t != tgt)
            outs = [p for p in splashes if p[4] == tgt and land - 1 <= p[0] <= land + 1]
            kinds = []
            for p in outs:
                t2 = p[5]
                lands = p[0] + p[3] // 30
                pooled = t2 in pools and pools[t2] < lands
                if t2 == tgt:
                    kinds.append('SPIT TILE')
                elif t2 in others:
                    kinds.append('other raider')
                else:
                    kinds.append('random ' + ('pooled' if pooled else 'fresh'))
                    random_n += 1
                    random_pooled += pooled
                    covered = sum(1 for c, t0 in pools.items() if t0 < lands
                                  and 0 <= c[0] - ARENA_ORIGIN[0] < ARENA and 0 <= c[1] - ARENA_ORIGIN[1] < ARENA)
                    pooled_share.append(covered / float(ARENA * ARENA))
                    inside += 0 <= t2[0] - ARENA_ORIGIN[0] < ARENA and 0 <= t2[1] - ARENA_ORIGIN[1] < ARENA
            shapes[(len(raiders), on_t, len(others), len(outs), tuple(sorted(kinds)))] += 1
    print("party, raiders on the spit tile, other raiders' tiles, splashes (where): landings")
    for key, c in sorted(shapes.items()):
        print('  party %d, on spit tile %d, other tiles %d, %d splash(es) %s: %d' % (key + (c,)))
    if random_n:
        print('random splashes %d: on a pool %d (%.0f%%), arena pooled then %.0f%%; inside the arena %d' % (
            random_n, random_pooled, 100.0 * random_pooled / random_n,
            100.0 * sum(pooled_share) / len(pooled_share), inside))


if __name__ == '__main__':
    main()
