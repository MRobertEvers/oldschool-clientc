# Ocellus phase 4 (oc_p4.py): the explosions: the landing ticks, and the stayers not hit.
# Run from this directory: python3 oc_p4_expl2.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
stay = Counter(); hit = Counter(); movehit = Counter()
miss = []
for f in fs:
    cur = {}; pre = {}
    for t in sorted(f.t):
        T = f.t[t]
        for n in f.raiders:
            p = T.pl.get(n)
            pre[(t, n)] = p.before if p else cur.get(n)
            if p: cur[n] = p.after
    for D in f.D:
        for L in range(5, 60):
            t = D + L
            if f.K and t > f.K: continue
            if t not in f.t or t-1 not in f.t: continue
            for n in f.raiders:
                p = f.t[t].pl.get(n)
                full = any(14 <= v <= 16 and ty in (16,17) for ty, v in (p.hits if p else []))
                a, b = pre.get((t-1, n)), pre.get((t, n))
                if a is None or b is None: continue
                if a == b:
                    stay[L] += 1
                    if full: hit[L] += 1
                    elif L in (10,12,14,16,18,20,41,43,45,47,49,51): miss.append((f.sid, t, 'D+%d' % L, n, a, p and p.hits))
                elif full: movehit[L] += 1
print('L stay hit movedhit')
for L in range(5, 60):
    if hit[L] or movehit[L]: print(L, stay[L], hit[L], movehit[L])
print('misses', miss)
