# Ocellus phase 4 (oc_p4.py): the void floor: raider-ticks in the ring / outside it by D, and the 10s.
# Run from this directory: python3 oc_p4_void.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
on = Counter(); hit10 = Counter(); ex = []
for f in fs:
    cur = {}; pre = {}
    for t in sorted(f.t):
        for n in f.raiders:
            p = f.t[t].pl.get(n)
            pre[(t, n)] = p.before if p else cur.get(n)
            if p: cur[n] = p.after
    bsw = None
    for D in f.D:
        for d in range(0, 60):
            t = D + d
            if f.K and t >= f.K: break
            T = f.t.get(t)
            if not T: continue
            b = T.npc.get(f.boss)
            if b: bsw = b.after
            for n in f.raiders:
                tile = pre.get((t, n)); p = T.pl.get(n)
                post = p.after if p else tile
                if tile is None or bsw is None: continue
                dpre = oc_p4.box(bsw, 3, tile); dpost = oc_p4.box(bsw, 3, post)
                h = [v for ty, v in (p.hits if p else []) if ty in (16, 17) and v == 10]
                on[(d, min(dpre, 3), min(dpost, 3))] += 1
                if h:
                    hit10[(d, min(dpre, 3), min(dpost, 3))] += 1
                    ex.append((f.sid, t, d, n, tile, post, dpre, dpost))
print('10s:', ex)
for d in list(range(0, 23)) + list(range(29, 52)):
    row = {k[1:]: on[k] for k in on if k[0] == d and ((d <= 21 and (k[1] <= 1 or k[2] <= 1)) or (d >= 29 and (k[1] >= 2 or k[2] >= 2)))}
    print(d, row)
