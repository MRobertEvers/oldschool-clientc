# Ocellus phase 4 (oc_p4.py): attacks made with the style his deflect prays
# against, and the hits on him and the attacker over the next ticks.

import sys; sys.path.insert(0, '.')
import oc_p4
fs = oc_p4.load()
MELEE = ('scythe', 'obligator_multihit', 'mace', 'whip', 'stab', 'slash', 'crush', 'punch', 'fang', 'rapier', 'claw')
RANGED = ('bow', 'zcb', 'xbow', 'blowpipe', 'glaive', 'dart', 'chin')
MAGIC = ('cast', 'staff', 'trident', 'shadow', 'sang')
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
                bad = (icon == (15,) and st == 'melee') or (icon == (16,) and st == 'ranged') or (st == 'magic' and icon in ((15,), (16,)))
                if bad:
                    later = []
                    for k in range(0, 4):
                        TT = f.t.get(t + k)
                        bb = TT and TT.npc.get(f.boss); pp = TT and TT.pl.get(n)
                        later.append('+%d b%s p%s' % (k, [v for ty, v in (bb.hits if bb else [])], [v for ty, v in (pp.hits if pp else [])]))
                    print(f.sid, t, 'D+%d' % ([t - D for D in f.D if t >= D][-1]), n, s, icon, ' | '.join(later))
