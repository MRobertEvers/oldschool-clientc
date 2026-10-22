# Ocellus phase 4 (oc_p4.py): the raiders attack gaps by D (the 2-tick window).
# Run from this directory: python3 oc_p4_fast.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter, defaultdict
fs = oc_p4.load()
ATT = ('scythe_of_vitur_attack', 'obligator_multihit', 'human_bow', 'zcb_attack', 'xbows_', 'mace', 'glaive', 'blowpipe', 'human_cast', 'spellcast', 'tumeken', 'staff', 'sang', 'shadow', 'trident', 'whip', 'rapier', 'fang', 'claw')
gap = defaultdict(Counter)
for f in fs:
    last = {}
    for t in sorted(f.t):
        if t > (f.K or f.last): break
        T = f.t[t]
        for n, p in T.pl.items():
            if n not in f.raiders: continue
            a = [s for s, dl in p.seqs if any(k in s for k in ATT) and 'block' not in s and 'def' not in s and 'selfimbue' not in s and 'resurrect' not in s and 'thrall' not in s]
            if not a: continue
            if t >= f.E and n in last:
                d = ([t - D for D in f.D if t >= D] or [None])[-1]
                g = t - last[n]
                gap[d][min(g, 9)] += 1
            last[n] = t
for d in sorted(k for k in gap if k is not None):
    c = gap[d]; tot = sum(c.values())
    print(d, tot, '2s=%d' % c[2], dict(sorted(c.items())))
