#!/usr/bin/env python3
"""walk.py: the cart walker over a group's plane 1 + plane 3 tracks.

A ride is a list of segments (tick gap, to1, to2, land, delay2, angle, sounds, plane).
Rules fitted to RSProx 4646 (docs/rsprox-analysis/quests/forgettable_tale.md, "The rides").
"""
import os, re, sys, json
S = os.path.dirname(os.path.abspath(__file__)) + '/'
GROUPS = {1: (2798, 3360), 2: (3576, 4076), 3: (4266, 4700)}

def snapshot(lo, hi, plane):
    locs = {}
    for line in open(S + 'tun.txt'):
        t, rest = line.split('\t', 1)
        t = int(t)
        if t < lo or t > hi:
            continue
        m = re.search(r'loc_add_change_v\d\] id=(\w+) \(\d+\), coord=\((\d+), (\d+), (\d+)\), shape=(\d+), rotation=(\d)', rest)
        if m:
            name, x, z, p, sh, rot = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))
            if p == plane and 'support' not in name:
                locs[(x, z, sh)] = (name, rot)
            continue
        m = re.search(r'loc_del\] coord=\((\d+), (\d+), (\d+)\), shape=(\d+)', rest)
        if m and int(m.group(3)) == plane:
            locs.pop((int(m.group(1)), int(m.group(2)), int(m.group(4))), None)
    tiles = {}
    for (x, z, sh), (name, rot) in locs.items():
        if sh == 22 or 'buffer' in name:
            tiles[(x, z)] = (name, rot)
    return tiles

class Grid:
    def __init__(self, tiles):
        self.t = tiles
    def kind(self, x, z):
        v = self.t.get((x, z))
        if not v:
            return None
        name, rot = v
        if 'straight' in name:
            return 'H' if rot in (0, 2) else 'V'
        if 'buffer' in name:
            return 'X'
        if name.startswith('keldagrim_track_tjunction_a') and rot == 0:
            return 'J'
        if 'curve' in name:
            return 'C'
        return 'M'   # any other junction piece

ANG = {'E': 1536, 'N': 1024, 'S': 0}

def east_scan(g, x, h):
    """From (x,h) heading east: (kind, x) of the first stop."""
    xx = x + 1
    while g.kind(xx, h) == 'H':
        xx += 1
    k = g.kind(xx, h)
    if k == 'J':
        return ('J', xx)
    if k == 'C':
        # a bend: down if the block reaches below the row
        return ('BS' if g.kind(xx, h - 1) in ('C',) else 'BN', xx)
    if k == 'X':
        return ('X', xx - 1)
    if k == 'M':
        return ('M', xx)
    return ('OPEN', xx - 1)

def vert_scan(g, c, z, s):
    zz = z + s
    while g.kind(c, zz) == 'V':
        zz += s
    k = g.kind(c, zz)
    if k in ('C', 'M'):
        return ('CORNER', zz)
    if k == 'X':
        return ('X', zz - s)
    return ('OPEN', zz - s)

if __name__ == '__main__':
    for gnum, (lo, hi) in GROUPS.items():
        g1 = Grid(snapshot(lo, hi, 1))
        g3 = Grid(snapshot(lo, hi, 3))
        js = sorted(k for k in g1.t if g1.kind(*k) == 'J')
        print(f'== group {gnum}: {len(js)} switch junctions')
        # every turn point reachable: junction exits and bends
        seen = set()
        todo = [('E', js[0][0], js[0][1], 'start')]
        stops = []
        while todo:
            hd, x, z, why = todo.pop()
            if (hd, x, z, why) in seen:
                continue
            seen.add((hd, x, z, why))
            # arrive at a turn point (x,z) heading east: what is here?
            k = g1.kind(x, z)
            if hd == 'E':
                if k == 'J':
                    for s in (1, -1):
                        todo.append(('V', x, z, 'N' if s == 1 else 'S'))
                    continue
            if hd == 'V':
                s = 1 if why == 'N' else -1
                kk, zc = vert_scan(g1, x + 1, z + s, s)
                if kk == 'CORNER':
                    h = zc + s
                    ke, xe = east_scan(g1, x + 2, h)
                    if ke == 'J':
                        todo.append(('E', xe, h, 'J'))
                    elif ke in ('BS', 'BN'):
                        todo.append(('V', xe, h, 'S' if ke == 'BS' else 'N'))
                        stops.append((f'bend {ke}', xe, h))
                    else:
                        stops.append((ke, xe, h, f'from J/bend ({x},{z}) {why}'))
                else:
                    stops.append(('VERT-' + kk, x + 1, zc, f'from ({x},{z}) {why}'))
        for st in stops:
            print('  ', st)
