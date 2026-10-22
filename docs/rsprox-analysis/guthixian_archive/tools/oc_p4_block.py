# Ocellus phase 4 (oc_p4.py): the raiders defend seq on each kind of hit.
# Run from this directory: python3 oc_p4_block.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
def mech(d, v):
    if d in (4, 35): return 'wind'
    if d in (29, 57): return 'hybrid'
    if d in (10,12,14,16,18,20,41,43,45,47,49,51) and 14 <= v <= 16: return 'expl'
    return 'other'
c = defaultdict(Counter)
for f in fs:
    for t in range(f.E, (f.K or f.last) + 1):
        T = f.t.get(t)
        if not T: continue
        d = ([t - D for D in f.D if t >= D] or [None])[-1]
        for n, p in T.pl.items():
            if n not in f.raiders: continue
            dmg = [v for ty, v in p.hits if ty in (16, 17, 12, 13)]
            if not dmg: continue
            m = mech(d, dmg[0])
            blk = [s for s in p.seqs if 'block' in s[0] or '_def' in s[0]]
            other = [s for s in p.seqs if s not in blk]
            prev = f.t.get(t-1)
            pb = [s for s in (prev.pl[n].seqs if prev and n in prev.pl else []) if 'block' in s[0] or '_def' in s[0]]
            key = ('block@t d%d' % blk[0][1]) if blk else ('block@t-1 d%d' % pb[0][1] if pb else ('attack' if other else 'none'))
            c[m][key] += 1
for m in c: print(m, dict(c[m]))
