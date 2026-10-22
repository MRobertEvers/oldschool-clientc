# Ocellus phase 4 (oc_p4.py): raider attacks by style under each of his overheads.
# Run from this directory: python3 oc_p4_swings.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
fs = oc_p4.load()
MELEE = ('scythe', 'obligator_multihit', 'mace', 'whip', 'stab', 'slash', 'crush', 'punch', 'fang', 'rapier', 'claw')
RANGED = ('bow', 'zcb', 'xbow', 'blowpipe', 'glaive', 'dart', 'chin')
MAGIC = ('cast', 'staff', 'trident', 'shadow', 'sang')
c = Counter()
for f in fs:
    icon = ()
    for t in range(f.E - 3, (f.K or f.last) + 1):
        T = f.t.get(t)
        if not T: continue
        b = T.npc.get(f.boss)
        if b and b.icons is not None: icon = tuple(b.icons)
        if t < f.E: continue
        for n, p in T.pl.items():
            if n not in f.raiders: continue
            for s, dl in p.seqs:
                if 'block' in s or '_def' in s or 'eat' in s: continue
                st = 'melee' if any(k in s for k in MELEE) else 'ranged' if any(k in s for k in RANGED) else 'magic' if any(k in s for k in MAGIC) else None
                if st: c[(icon, st)] += 1
for k in sorted(c): print(k, c[k])
