# Ocellus phase 4 (oc_p4.py): every raider splat of phase 4 by its tick in the 60-tick cycle (D = deflect_in).
# Run from this directory: python3 oc_p4_splats.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import defaultdict, Counter
fs = oc_p4.load()
by = defaultdict(list)
for f in fs:
    end = f.K or f.last
    for t in range(f.E, end + 1):
        T = f.t.get(t)
        if not T: continue
        # cycle offset
        d = None
        for D in f.D:
            if t >= D: d = t - D
        for n, p in T.pl.items():
            if n not in f.raiders: continue
            for ty, v in p.hits:
                by[d].append((ty, v))
for d in sorted(by, key=lambda x: -1 if x is None else x):
    c = by[d]
    vals = sorted(v for ty, v in c if ty in (16,17))
    heals = sorted(v for ty, v in c if ty == 6)
    zeros = sum(1 for ty, v in c if ty in (12,13))
    other = Counter(ty for ty, v in c if ty not in (16,17,6,12,13))
    print(d, 'n', len(c), 'dmg', vals[:30], 'z', zeros, 'heal', heals, dict(other) if other else '')
