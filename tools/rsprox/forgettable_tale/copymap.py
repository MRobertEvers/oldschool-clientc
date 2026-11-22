#!/usr/bin/env python3
"""copymap.py: where a template tile of the tunnels lands in Jagex's copy.

Jagex builds each group's copy of the tunnels (4646 [2798], [3580], [4270]
rebuild_region) from the template square, levels 0 and 1, with the platforms'
zones -- template planes 2 and 3 -- copied over planes 0 and 1 just past the
tunnel mouths. A cart rolls out of a mouth straight onto its platform: no ride
changes plane. The default transcript translates those zones back to their
template (plane 2/3) tiles, which is what walk.py and gen.py read; this maps
them into the copy again. One template zone can be copied to two places
(group 1's 5,7), so a tile past a mouth goes to the copy nearest the step
before it.

    python3 copymap.py     # must print "copy paths matched 20 of 20"

It needs the --raw transcript (copy coordinates) and the all-filters one
(rebuild_region, for each copy's corner): README.md.
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# dest zone (copy-local) <- template zone (local); dest plane = template plane - 2
SUBS = {1: {(4, 3): (5, 7), (6, 5): (5, 7), (6, 6): (7, 7)},
        2: {(5, 3): (5, 7), (6, 2): (6, 7), (6, 5): (7, 7)},
        3: {(4, 4): (6, 7), (6, 4): (5, 7)}}
RAW = os.path.expanduser('~/.rsprox-index-cache/home-raw/.rsprox/binary/archive/idx4646.txt')
WIRE = os.path.expanduser('~/.rsprox-index-cache/home-wire/.rsprox/binary/archive/idx4646.txt')


def copy_tile(g, x, z, p, near):
    """A template tile (absolute) -> copy-local (lx, lz, plane)."""
    lx, lz = x - 1856, z - 4928
    if p <= 1:
        return (lx, lz, p)
    src = (lx // 8, lz // 8)
    cands = [d for d, s in SUBS[g].items() if s == src]
    assert cands, ('a plane-%d tile in a zone group %d never copies' % (p, g), x, z)
    best = min(cands, key=lambda d: abs(d[0] * 8 + lx % 8 - near[0]) + abs(d[1] * 8 + lz % 8 - near[1]))
    return (best[0] * 8 + lx % 8, best[1] * 8 + lz % 8, p - 2)


def copies_of(g, x, z, p):
    """Every copy-local tile a template tile is laid at (a loc on a platform)."""
    lx, lz = x - 1856, z - 4928
    if p <= 1:
        return [(lx, lz, p)]
    src = (lx // 8, lz // 8)
    return [(d[0] * 8 + lx % 8, d[1] * 8 + lz % 8, p - 2) for d, s in sorted(SUBS[g].items()) if s == src]


def corners():
    """(tick, corner x, corner z) of every whole-square tunnel copy."""
    out = []
    tick = 0
    cur = None
    for line in open(WIRE, encoding='utf-8', errors='replace'):
        m = re.match(r'^\[(\d+)\]', line)
        if m:
            tick = int(m.group(1))
            continue
        if '[rebuild_region_v2]' in line:
            cur = [tick, []]
            out.append(cur)
            continue
        if cur is not None:
            m = re.search(r'dest=\((\d+), (\d+), \d\)', line)
            if m:
                cur[1].append((int(m.group(1)), int(m.group(2))))
            elif line.startswith('    ['):
                cur = None
    return [(t, min(d[0] for d in z), min(d[1] for d in z)) for t, z in out if len(z) >= 100]


def raw_pieces(lo, hi):
    """(name, lx, lz, plane) of every track piece Jagex added in ticks lo..hi, copy-local."""
    cs = corners()
    out = set()
    tick = 0
    for line in open(RAW, encoding='utf-8', errors='replace'):
        m = re.match(r'^\[(\d+)\]', line)
        if m:
            tick = int(m.group(1))
            if tick > hi:
                break
            continue
        if tick < lo:
            continue
        m = re.search(r'loc_add_change_v\d\] id=(\w+) \(\d+\), coord=\((\d+), (\d+), (\d)\)', line)
        if m:
            o = [c for c in cs if c[0] <= tick][-1]
            name = m.group(1).replace('_a_left', '_a').replace('_a_right', '_a').replace('card_box_open', 'card_box')
            out.add((name, int(m.group(2)) - o[1], int(m.group(3)) - o[2], int(m.group(4))))
    return out


def raw_rides():
    cs = corners()
    tick = 0
    rides = {}
    cur = None
    for line in open(RAW, encoding='utf-8', errors='replace'):
        m = re.match(r'^\[(\d+)\]', line)
        if m:
            tick = int(m.group(1))
            continue
        if 'oploc1_v3] id=forget_train_cart' in line:
            cur = rides.setdefault(tick, [])
            continue
        if cur is None:
            continue
        m = re.search(r'\[teleport\] player=.*newcoord=\((\d+), (\d+), (\d)\)', line)
        if m:
            o = [c for c in cs if c[0] <= tick][-1]
            cur.append((int(m.group(1)) - o[1], int(m.group(2)) - o[2], int(m.group(3))))
            continue
        if re.search(r'message_game\] type=(dialogue|mesbox)', line) and cur:
            cur = None
    return rides


if __name__ == '__main__':
    import gen
    raw = raw_rides()
    B = {g: gen.Build(g) for g in (1, 2, 3)}
    for b in B.values():
        b.build()
    ok = n = 0
    for g, t, lay, end, pts in gen.recorded():
        n += 1
        sim, res = B[g].simulate(lay)
        mapped = []
        prev = (7, 29, 1)
        for p in sim:
            prev = copy_tile(g, p[0], p[1], p[2], prev)
            mapped.append(prev)
        rp = raw[t][1:1 + len(mapped)]
        if mapped == rp:
            ok += 1
        else:
            print('MISMATCH g%d [%d] %s\n  raw %s\n  map %s' % (g, t, res, rp, mapped))
    print('copy paths matched', ok, 'of', n)
