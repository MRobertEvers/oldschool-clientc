# Ocellus phase 4 (oc_p4.py): every raider attack against him with the style
# his deflect prays against, its own landing (bows by their projectile; a
# melee swing on its tick) and the attacker's hits the next 2 ticks; and the
# same for a sample of attacks with the other style, as the control.
# Run from this directory: python3 oc_p4_reflect.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
MELEE = ('scythe', 'obligator_multihit', 'mace', 'whip', 'stab', 'slash', 'crush', 'punch', 'fang', 'rapier', 'claw')
RANGED = ('human_bow', 'zcb', 'xbow', 'blowpipe', 'glaive', 'dart', 'chin')
ctrl = Counter()
for f in oc_p4.load():
    icon = ()
    for t in range(f.E - 3, (f.K or f.last) + 1):
        T = f.ticks.get(t)
        if not T: continue
        b = T.npc.get(f.boss)
        if b and b.icons is not None: icon = tuple(b.icons)
        if t < f.E: continue
        for n, p in T.pl.items():
            if n not in f.raiders: continue
            face = ' '.join(p.facing)
            for s, dl in p.seqs:
                st = 'melee' if any(k in s for k in MELEE) else 'ranged' if any(k in s for k in RANGED) else None
                if not st or 'block' in s or '_def' in s: continue
                # at him (not a rift): his face block, or no face change and he is the nearest target
                if 'bombportal' in face: continue
                bad = (icon == (15,) and st == 'melee') or (icon == (16,) and st == 'ranged')
                nxt = [[v for ty, v in (f.ticks[t + k].pl[n].hits if n in f.ticks.get(t + k, oc_p4.Tick()).pl else []) if ty in (16, 17)] for k in (1, 2)]
                small = any(1 <= v <= 5 for v in nxt[0])
                if bad:
                    land = None
                    for k, sx in T.top:
                        m = re.search(r'endtime=(\d+).*\[source\] coord=\((\d+), (\d+)', sx)
                        if m and 'projanim' in sx and (int(m.group(2)), int(m.group(3))) == p.after:
                            land = t + (int(m.group(1)) + 15) // 30
                    lt = land if st == 'ranged' else t
                    bh = f.ticks[lt].npc[f.boss].hits if lt and f.boss in f.ticks[lt].npc else None
                    d = [t - D for D in f.D if t >= D][-1]
                    print(f.sid, t, 'D+%d' % d, n[:9], st, 'icon', icon, 'lands', lt and lt - t, 'him then', bh, 'me +1,+2', nxt)
                elif icon in ((15,), (16,)):
                    ctrl[(st, small)] += 1
print('control (the style not deflected): (style, small hit 1..5 on me at +1) ->', dict(ctrl))
