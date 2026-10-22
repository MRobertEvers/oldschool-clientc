# Ocellus phase 4 (oc_p4.py): phase 4 rifts: adds per cycle, tiles from his 3x3, lives.
# Run from this directory: python3 oc_p4_rifts.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
for f in fs:
    life = {}
    bsw = f.t[f.E].npc[f.boss].after
    for t in range(f.E, (f.K or f.last) + 17):
        T = f.t.get(t)
        if not T: continue
        d = ([t - D for D in f.D if t >= D] or [None])[-1]
        for i, n in T.npc.items():
            if 'bombportal' not in n.type and 'rift' not in n.type: continue
            if n.move == 'add':
                life[i] = [t, d, n.type, (n.after[0] - bsw[0], n.after[1] - bsw[1]), n.other[:1], n.seqs[:1], None, [], 0]
            L = life.get(i)
            if not L: continue
            L[8] += len([h for h in n.hits if h[0] in (16, 17, 12, 13)])
            for s, dl in n.seqs:
                if 'despawn' in s or 'death' in s or 'attack' in s or 'charge' in s or 'explode' in s: L[7].append((t - L[0], s))
            for s in n.spots:
                if 'charge' in s[1] or 'explo' in s[1]: L[7].append((t - L[0], s[1]))
            if n.move == 'del': L[6] = t - L[0]
    by = defaultdict(list)
    for i, L in life.items(): by[(L[0], L[1])].append(L)
    for (t, d), Ls in sorted(by.items()):
        print(f.sid, t, 'D+%s' % d, len(Ls), [(L[3], L[4][0][:22] if L[4] else '', 'life', L[6], 'hits', L[8], L[7][:3]) for L in Ls])
