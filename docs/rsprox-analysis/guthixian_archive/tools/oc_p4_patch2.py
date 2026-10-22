# Ocellus phase 4 (oc_p4.py): phase-3 void patches: hits by ticks since the raider s last patch hit.
# Run from this directory: python3 oc_p4_patch2.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
res = Counter()
for sid in oc_p4.SESSIONS:
    pass
for f in oc_p4.load():
    hist = {}
    for t in sorted(f.ticks):
        if t > (f.K or f.last): break
        for k, s in f.ticks[t].top:
            m = re.match(r'\[loc_add_change_v2\] id=([a-z0-9_]+) .*coord=\((\d+), (\d+), 1\), shape=22', s)
            if m:
                hist.setdefault((int(m.group(2)), int(m.group(3))), []).append((t, m.group(1)))
            m = re.match(r'\[loc_del\w*\] coord=\((\d+), (\d+), 1\), shape=22', s)
            if m:
                hist.setdefault((int(m.group(1)), int(m.group(2))), []).append((t, 'DEL'))
    def at(tile, t):
        cur = None
        for u, l in hist.get(tile, ()):
            if u < t: cur = (u, l)
        return cur if cur and 'degen' in cur[1] else None
    pre = {}; c = {}
    for t in sorted(f.ticks):
        if t < f.E - 400 or t > (f.K or f.last): continue
        for n in f.raiders:
            p = f.ticks[t].pl.get(n); pre[(t, n)] = p.before if p else c.get(n)
            if p: c[n] = p.after
    # phase 3 AND 4: every raider tick with pre-tile on a live patch
    lasthit = {}
    for t in range(f.E - 400, (f.K or f.last)):
        T = f.ticks.get(t)
        if not T: continue
        for n in f.raiders:
            a = pre.get((t, n))
            b = at(a, t) if a else None
            if b:
                born = {a: b}
                p = T.pl.get(n)
                hv = [v for ty, v in (p.hits if p else []) if ty in (16, 17, 12, 13)]
                h67 = any(v in (6, 7) for v in hv)
                prevon = pre.get((t - 1, n)) == a
                k = t - lasthit.get(n, -99)
                res[('since last patch hit', min(k, 9), 'stay' if prevon else 'new', 'H' if h67 else ('other' if hv else '-'))] += 1
                if h67: lasthit[n] = t
                if k >= 9 and not h67:
                    print('MISS', f.sid, t, n[:8], 'patch', born[a][1][-12:], 'age', t - born[a][0], 'stay' if prevon else 'new', hv, 'pre-1', pre.get((t-1,n)) == a, 'pre-2', pre.get((t-2,n)) == a, 'E%+d' % (t - f.E))
for k in sorted(res): print(k, res[k])
