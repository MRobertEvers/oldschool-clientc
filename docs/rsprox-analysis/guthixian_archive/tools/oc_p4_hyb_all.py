# Ocellus phase 4 (oc_p4.py): each hybrid cast against the raiders overheads (--bas transcripts).
# Run from this directory: python3 oc_p4_hyb_all.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
ICON = {0: 'melee', 1: 'ranged', 2: 'magic'}
vals = Counter(); mixed = []
for sid in oc_p4.SESSIONS:
    import pickle, os
    ticks = pickle.load(open(os.path.join(oc_p4.ARCH, 'idx%d.txt.oc_p4.pkl' % sid), 'rb'))
    order = sorted(ticks)
    icon = {}; raiders = set()
    for t in order:
        for n, p in ticks[t].pl.items():
            if p.icon is not None: icon[n] = p.icon
        for i, b in ticks[t].npc.items():
            if not b.type.startswith('tfa_ocellus_p3'): continue
            k = [s[1] for s in b.spots if 'melee_hybrid' in s[1]]
            if not k: continue
            kind = k[0].replace('vfx_ocellus_phase3_melee_hybrid', '') or '_melee'
            got = []
            for L in (t + 5,):
                T = ticks.get(L)
                for n, p in (T.pl.items() if T else []):
                    for ty, v in p.hits:
                        if ty in (16, 17, 12, 13): got.append((n, v, ICON.get(icon.get(n), icon.get(n))))
            vs = set(v for n, v, ic in got)
            print(sid, t, kind, sorted(got, key=lambda x: x[0]))
