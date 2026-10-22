# Ocellus phase 4 (oc_p4.py): each hybrid cast's value by the cast's number
# in the fight, prayed (the overhead on the land tick matching its kind) and
# not. Run from this directory: python3 oc_p4_hyb_cycle.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import defaultdict
KIND = {'_magic': 2, '_ranged': 1, '_melee': 0}
by_n = defaultdict(list); unprayed = []
for f in oc_p4.load():
    # every tick's overhead, carried from the session's start
    icon = {}; icon_at = {}
    for t in sorted(f.ticks):
        if t > (f.K or f.last) + 6: break
        for n, p in f.ticks[t].pl.items():
            if p.icon is not None: icon[n] = p.icon
        if t >= f.E: icon_at[t] = dict(icon)
    n = 0
    for D in f.D:
        for c in (D + 24, D + 52):
            T = f.t.get(c); b = T and T.npc.get(f.boss)
            if not b: continue
            k = [s[1] for s in b.spots if 'melee_hybrid' in s[1]]
            if not k: continue
            kind = k[0].replace('vfx_ocellus_phase3_melee_hybrid', '') or '_melee'
            L = c + 5
            TL = f.t.get(L)
            vals = []
            for nm in f.raiders:
                p = TL and TL.pl.get(nm)
                v = [v for ty, v in (p.hits if p else []) if ty in (16, 17, 12, 13)]
                if not v: continue
                prayed = icon_at.get(L, {}).get(nm) == KIND[kind]
                vals.append((nm[:6], v, prayed))
                if not prayed: unprayed.append((f.sid, L, kind, nm, v, icon_at.get(L, {}).get(nm)))
            pv = sorted(set(v[0] for _, v, pr in vals if pr))
            by_n[n].append(pv)
            print(f.sid, c, 'cast', n, kind, vals)
            n += 1
print('--- prayed value(s) by cast number')
for n in sorted(by_n): print(n, by_n[n])
print('--- not prayed at landing')
for u in unprayed: print(u)
