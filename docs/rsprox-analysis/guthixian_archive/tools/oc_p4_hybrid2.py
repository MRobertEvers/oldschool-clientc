# Ocellus phase 4 (oc_p4.py): each hybrid cast: kind, and every raider hit with it (one value a cast).
# Run from this directory: python3 oc_p4_hybrid2.py
import sys; sys.path.insert(0, '.')
import oc_p4
fs = oc_p4.load()
for f in fs:
    st = oc_p4.state(f)
    for D in f.D:
        for c in (D + 24, D + 52):
            T = f.t.get(c); b = T and T.npc.get(f.boss)
            if not b: continue
            kind = [s[1] for s in b.spots if 'hybrid' in s[1]]
            if not kind: continue
            k = kind[0].replace('vfx_ocellus_phase3_melee_hybrid', '') or '_melee'
            vals = []
            for L in range(c + 4, c + 7):
                TL = f.t.get(L)
                for n in f.raiders:
                    p = TL and TL.pl.get(n)
                    if p:
                        for ty, v in p.hits:
                            if ty in (16,17,12,13): vals.append((n[:5], L - c, v, oc_p4.box(b.after, 3, st[L][0][n])))
            print(f.sid, c, 'D+%d' % (c - D), k, vals)
