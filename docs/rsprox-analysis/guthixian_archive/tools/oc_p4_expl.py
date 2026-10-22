# Ocellus phase 4 (oc_p4.py): the explosions: which tile is judged (start-of-tick vs after the move), at start and landing.
# Run from this directory: python3 oc_p4_expl.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
def cheb(a, b): return max(abs(a[0]-b[0]), abs(a[1]-b[1]))
# per (start-tile model, land-tile model): Counter of (dist, hitclass)
LANDS = [10,12,14,16,18,20] + [41,43,45,47,49,51]
res = defaultdict(Counter)
alive = 0
for f in fs:
    # each raider's tile before and after every tick
    pre, post = {}, {}
    cur = {}
    for t in sorted(f.t):
        T = f.t[t]
        for n in f.raiders:
            p = T.pl.get(n)
            b = p.before if p else cur.get(n)
            a = p.after if p else cur.get(n)
            pre[(t, n)] = b; post[(t, n)] = a
            if a: cur[n] = a
    for D in f.D:
        for L in LANDS:
            t = D + L
            if f.K and t > f.K: continue
            if t not in f.t: continue
            for n in f.raiders:
                p = f.t[t].pl.get(n)
                hits = [v for ty, v in (p.hits if p else []) if ty in (16, 17, 12, 13)]
                ex = [v for v in hits if 5 <= v <= 16]
                cls = 'full' if any(14 <= v <= 16 for v in ex) else ('part' if ex else 'none')
                if pre.get((t-1, n)) is None: continue
                for sm in ('pre', 'post'):
                    st = (pre if sm == 'pre' else post)[(t-1, n)]
                    for lm in ('pre', 'post'):
                        lt = (pre if lm == 'pre' else post)[(t, n)]
                        if st and lt:
                            res[(sm, lm)][(min(cheb(st, lt), 3), cls)] += 1
for k, c in res.items():
    print('start', k[0], 'land', k[1], sorted(c.items()))
