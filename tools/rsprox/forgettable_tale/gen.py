#!/usr/bin/env python3
"""gen.py: the three tunnel groups' complete track graphs -> RuneScript, validated
against every recorded ride of RSProx index 4646."""
import os, re, sys, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import walk

E, N, SO = 1536, 1024, 0
HUB_START = (1864, 4957, 1)

def lit(x, z, p):
    return f'{p}_{x // 64}_{z // 64}_{x % 64}_{z % 64}'

# ---- terminals reached through a tunnel mouth, keyed (group, node x, node z, dir) ----
# 'P<n>': platform n (arrival tail of plane-3 positions); 'ROOM'; 'BRIDGE'; 'PX': plane-3 dead end.
PORTALS = {
    1: {(1881, 4948, 'N'): ('P', 1, [(1897, 4987, 3)]),
        (1897, 4961, 'N'): ('P', 2, [(1897, 4984, 3)]),
        (1897, 4974, 'S'): ('P', 2, [(1897, 4984, 3)]),
        (1899, 4949, 'N'): ('ROOM', 0, []),
        (1897, 4974, 'N'): ('P', 3, [(1913, 4990, 3)])},          # unrecorded: the mouth's end, then a run to the cart (TAILS)
    2: {(1887, 4962, 'S'): ('P', 1, [(1898, 4987, 3)]),
        (1896, 4968, 'S'): ('BRIDGE', 0, [(1897, 4991, 3)]),
        (1904, 4958, 'S'): ('PX', 0, [(1909, 4991, 3)]),
        (1911, 4963, 'S'): ('ROOM', 0, []),
        (1899, 4943, 'N'): ('P', 2, [(1904, 4987, 3)]),
        (1906, 4976, 'S'): ('P', 3, [(1915, 4988, 3)])},
    3: {(1901, 4955, 'N'): ('P', 2, [(1897, 4984, 3)]),
        (1886, 4965, 'S'): ('P', 1, [(1906, 4984, 3)]),
        (1908, 4969, 'S'): ('ROOM', 0, [(1901, 4989, 3)])},
}
BRIDGE_EXIT = {2: (1904, 4958)}
# RSProx 4646: the move through each mouth (tiles, delay2); unrecorded mouths 3, 60.
FINAL = {(1, 1881, 4948, 'N'): (3, 60), (1, 1897, 4961, 'N'): (3, 60), (1, 1897, 4974, 'S'): (3, 60),
         (2, 1887, 4962, 'S'): (3, 60), (2, 1896, 4968, 'S'): (2, 60), (2, 1904, 4958, 'S'): (3, 75),
         (2, 1899, 4943, 'N'): (3, 75), (2, 1906, 4976, 'S'): (3, 75),
         (3, 1886, 4965, 'S'): (2, 130), (3, 1901, 4955, 'N'): (2, 130), (3, 1908, 4969, 'S'): (3, 90)}
# where the move through a mouth starts, when the snapshot's last tile is short
FROM = {(2, 1887, 4962, 'S'): (1895, 4955)}
# moves past a mouth, in template tiles (to1, to2, delay2, angle, land); copymap.py places them
TAILS = {('P', 2, 2): [((1904, 4987, 3), (1906, 4987, 3), 130, 1536, (1906, 4987, 3))],
         ('P', 2, 3): [((1915, 4988, 3), (1915, 4986, 3), 60, 0, (1915, 4986, 3)),
                       ((1915, 4986, 3), (1916, 4986, 3), 80, 1536, (1917, 4985, 3))],
         ('BRIDGE', 2, 0): [((1897, 4991, 3), (1900, 4991, 3), 75, 1536, (1901, 4990, 3)),
                            ((1901, 4990, 3), (1904, 4958, 1), 90, 1536, (1904, 4958, 1))],  # --raw [4066]: the slide ends on plane 1, off the bridge
         # group one's third platform (unrecorded): in the copy the mouth's end is on the platform,
         # three tiles short of its cart, where every recorded platform stops (copymap.py)
         ('P', 1, 3): [((1913, 4990, 3), (1916, 4990, 3), 60, 1536, (1916, 4990, 3))],
         # group three's room: two moves past the mouth before 119 (4646 [4694], [4695])
         ('ROOM', 3, 0): [((1901, 4989, 3), (1912, 4965, 1), 75, 1536, (1913, 4964, 1)),  # --raw [4694]: the slide's end is off the platform zone
                          ((1913, 4964, 1), (1916, 4964, 1), 60, 1536, (1916, 4964, 1))]}

# forget_cart_turn sounds on each TAILS step (4646: [3847..3848], [4002..4003], [4694..4695],
# [3046]), and the extra one the step through a mouth plays when the run turns straight
# after it (the bridge [3846], [4064]; room six [4692]; no platform's).
MOUTH_TURNS = {('BRIDGE', 2, 0): 1, ('ROOM', 3, 0): 1}
TAIL_TURNS = {('BRIDGE', 2, 0): [1, 1], ('P', 2, 3): [1, 1], ('ROOM', 3, 0): [1, 0],
              ('P', 2, 2): [0], ('P', 1, 3): [0]}

class Build:
    def __init__(self, g):
        lo, hi = walk.GROUPS[g]
        self.g = g
        self.G = walk.Grid(walk.snapshot(lo, hi, 1))
        self.js = sorted(k for k in self.G.t if self.G.kind(*k) == 'J')
        self.hub = self.js[0]
        self.nodes = {}     # (x,z) -> {'type': 'J'|'B', 'dir': fixed for B}
        self.edges = {}     # ((x,z), dir) -> (segments, terminal)
        self.unknown = []

    # ---- geometry ----
    def corner(self, c, z0, s):
        zz = z0
        while self.G.kind(c, zz) == 'V':
            zz += s
        return zz, self.G.kind(c, zz)

    def exit_row(self, c, zc, s):
        for r in (zc + s, zc, zc - s):
            if self.G.kind(c + 2, r) in ('H', 'J', 'C', 'M', 'X'):
                return r
        return zc + s

    def east_stop(self, c, r):
        k, xe = walk.east_scan(self.G, c + 1, r)
        return k, xe

    # ---- segments ----
    @staticmethod
    def runsegs(x0, z0, x1, z1, ang, p=1):
        segs = []
        x, z = x0, z0
        while (x, z) != (x1, z1):
            if x1 != x:
                d = max(-3, min(3, x1 - x)); nx, nz = x + d, z
            else:
                d = max(-3, min(3, z1 - z)); nx, nz = x, z + d
            n = abs(nx - x) + abs(nz - z)
            segs.append([(x, z, p), (nx, nz, p), 60 if n >= 2 else 40, ang, (nx, nz, p), 0])
            x, z = nx, nz
        return segs

    def edge(self, node, d):
        """Segments from turn point `node` taking exit d ('N'/'S'): until the next
        node, a dead end, or a mouth."""
        tx, tz = node
        s = 1 if d == 'N' else -1
        ang = N if s == 1 else SO
        c = tx + 1
        zc, k = self.corner(c, tz + 2 * s, s)
        segs = []
        if k not in ('C', 'M'):
            # a vertical end: a buffer or a mouth
            if k == 'X':
                stop = zc - s
            else:
                stop = zc - s
            m = min(4, abs(stop - tz))
            segs.append([(tx, tz, 1), (tx, tz + s * (m - 1), 1), 75 if m == 4 else (60 if m >= 3 else 40), ang, (c, tz + s * m, 1), 1])
            segs += self.runsegs(c, tz + s * m, c, stop, ang)
            if k == 'X':
                return segs, ('X',)
            return self.mouth(node, d, segs, (c, stop), ang)
        m = min(4, abs(zc - tz))
        segs.append([(tx, tz, 1), (tx, tz + s * (m - 1), 1), 75 if m == 4 else (60 if m >= 3 else 40), ang, (c, tz + s * m, 1), 1])
        segs += self.runsegs(c, tz + s * m, c, zc, ang)
        if segs:
            segs[-1][5] += 1
        r = self.exit_row(c, zc, s)
        k2, xe = self.east_stop(c, r)
        m = min(4, xe - c) if xe > c else 1
        segs.append([(c, zc, 1), (c + m - 1, zc, 1), 75 if m == 4 else (60 if m >= 3 else 40), E, (c + m, r, 1), 1])
        if k2 == 'OPEN':
            segs += self.runsegs(c + m, r, xe, r, E)
            return self.mouth(node, d, segs, (xe, r), E)
        if k2 == 'X':
            segs += self.runsegs(c + m, r, xe, r, E)
            return segs, ('X',)
        if k2 in ('BS', 'BN'):
            segs += self.runsegs(c + m, r, xe, r, E)
            segs[-1][5] += 1 if len(segs) > 1 else 0
            self.nodes[(xe, r)] = {'type': 'B', 'dir': 'S' if k2 == 'BS' else 'N'}
            return segs, ('NODE', (xe, r))
        if k2 == 'J':
            self.nodes.setdefault((xe, r), {'type': 'J'})
            return segs, ('J', (xe, r), c + m)
        self.unknown.append((node, d, k2, xe, r))
        return segs, ('?',)

    def mouth(self, node, d, segs, last, ang):
        key = (node[0], node[1], d)
        portal = PORTALS[self.g].get(key)
        if portal is None:
            self.unknown.append(('mouth', key, last))
            return segs, ('?',)
        kind = portal[0]
        if kind == 'ROOM' and not portal[2]:
            return segs, ('MOUTH', portal, None)
        # the move that would arrive at the last tile goes through the mouth
        arr = segs.pop()
        pin = FROM.get((self.g,) + key)
        if pin:
            segs.append(arr)
            segs += self.runsegs(arr[4][0], arr[4][1], pin[0], pin[1], arr[3])
            arr = [(pin[0], pin[1], 1), None, 0, arr[3], None, 0]
        dist, delay = FINAL.get((self.g,) + key, (3, 60))
        (x, z, p), ang2 = arr[0], arr[3]
        dx, dz = {E: (1, 0), N: (0, 1), SO: (0, -1)}[ang2]
        segs.append([(x, z, p), (x + dx * dist, z + dz * dist, p), delay, ang2, 'PORTAL', arr[5]])
        return segs, ('MOUTH', portal, None)

    def build(self):
        todo = [self.hub]
        if self.g in BRIDGE_EXIT:
            todo.append(BRIDGE_EXIT[self.g])
            self.nodes[BRIDGE_EXIT[self.g]] = {'type': 'J'}
        self.nodes[self.hub] = {'type': 'J'}
        done = set()
        while todo:
            n = todo.pop()
            if n in done:
                continue
            done.add(n)
            info = self.nodes[n]
            dirs = ['N', 'S'] if info['type'] == 'J' else [info['dir']]
            for d in dirs:
                segs, term = self.edge(n, d)
                self.edges[(n, d)] = (segs, term)
                if term[0] in ('J', 'NODE'):
                    todo.append(term[1])
                if term[0] == 'MOUTH' and term[1][0] == 'BRIDGE':
                    todo.append(BRIDGE_EXIT[self.g])
        # switch indices: breadth-first, north first, over switch junctions
        order = []
        q = [self.hub]
        seen = set()
        while q:
            n = q.pop(0)
            if n in seen:
                continue
            seen.add(n)
            if self.nodes[n]['type'] == 'J':
                order.append(n)
            dirs = ['N', 'S'] if self.nodes[n]['type'] == 'J' else [self.nodes[n]['dir']]
            for d in dirs:
                segs, term = self.edges[(n, d)]
                if term[0] in ('J', 'NODE'):
                    q.append(term[1])
                if term[0] == 'MOUTH' and term[1][0] == 'BRIDGE':
                    q.append(BRIDGE_EXIT[self.g])
        if self.g == 3:
            a, b = order.index((1908, 4937)), order.index((1908, 4969))
            order[a], order[b] = order[b], order[a]
        self.order = order
        self.idx = {n: i for i, n in enumerate(order)}

    # ---- simulate a layout -> TP positions ----
    def simulate(self, lay):
        pos = []
        # the run from the hub start to the hub junction
        hx, hz = self.hub
        def arrive_j(n, x_from, z_from):
            st = int(lay[self.idx[n]])
            tx = n[0] if st else n[0] - 1
            for sg in self.runsegs(x_from, z_from, tx, n[1], E):
                pos.append(sg[4])
            return st
        cur = self.hub
        st = arrive_j(cur, HUB_START[0], HUB_START[1])
        while True:
            if st == 0:
                return pos, 'blocked'
            info = self.nodes[cur]
            d = info.get('dir') if info['type'] == 'B' else ('N' if st == 1 else 'S')
            segs, term = self.edges[(cur, d)]
            for sg in segs:
                pos.append(sg[4])
            if term[0] == 'J':
                cur = term[1]
                # segs ended at the landing c+m; the run to the junction is part of arrive
                st = arrive_j(cur, pos[-1][0], pos[-1][1])
                continue
            if term[0] == 'NODE':
                cur = term[1]; st = 1
                continue
            if term[0] == 'X':
                return pos, 'dead end'
            if term[0] == 'MOUTH':
                kind, num, tail = term[1]
                if segs and segs[-1][4] == 'PORTAL':
                    pos.pop()
                    pos += tail
                for t in TAILS.get((kind, self.g, num), []):
                    pos.append(t[4])
                if kind == 'BRIDGE':
                    cur = BRIDGE_EXIT[self.g]
                    st = int(lay[self.idx[cur]])
                    continue
                return pos, f'{kind}{num}'
            return pos, '?'

def recorded():
    out = subprocess.run(['python3', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rides.py')], capture_output=True, text=True).stdout.split('\n')
    rides = []
    for i in range(0, len(out) - 1, 2):
        p = out[i + 1].split()
        if not p:
            continue
        m = re.match(r'\[(\d+)\] st(\d+) lay=(\d+) end=(.*)', out[i])
        pts = []
        for q in p:
            a = q.split('/'); x, z = map(int, a[0].split(',')); pl = int(a[1]) if len(a) > 1 else 1
            pts.append((x, z, pl))
        st = int(m.group(2))
        g = 1 if st <= 105 else (2 if st <= 115 else 3)
        rides.append((g, int(m.group(1)), m.group(3), m.group(4), pts))
    return rides

if __name__ == '__main__':
    B = {g: Build(g) for g in (1, 2, 3)}
    for g, b in B.items():
        b.build()
        print(f'group {g}: {len(b.order)} switches, {len(b.edges)} edges, unknown {b.unknown}')
        for i, n in enumerate(b.order):
            print(f'   {i}: J{n}', ' '.join(f"{d}->{b.edges[(n, d)][1][0]}{b.edges[(n,d)][1][1] if b.edges[(n,d)][1][0] in ('J','NODE') else (b.edges[(n,d)][1][1][:2] if b.edges[(n,d)][1][0]=='MOUTH' else '')}" for d in 'NS'))
    ok = bad = 0
    for g, t, lay, end, pts in recorded():
        b = B[g]
        sim, res = b.simulate(lay)
        rec = pts[1:]   # the first recorded point is the boarding step
        if res.startswith('ROOM'):
            rec = rec[:len(sim)]   # the scene's own moves follow the ride
        same = [p[:2] for p in sim] == [p[:2] for p in rec]
        if same:
            ok += 1
        else:
            bad += 1
            print(f'MISMATCH g{g} [{t}] lay={lay} -> {res}')
            print('   rec', ' '.join(f'{p[0]},{p[1]}' for p in rec))
            print('   sim', ' '.join(f'{p[0]},{p[1]}' for p in sim))
    print('rides matched', ok, 'of', ok + bad)
