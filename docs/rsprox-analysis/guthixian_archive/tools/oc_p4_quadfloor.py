# Ocellus phase 4 (oc_p4.py): the quadrant floor (offsets from the arena middle) and the rift and orb tiles.
# Run from this directory: python3 oc_p4_quadfloor.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
fs = oc_p4.load()
MID = (4127, 5673)
offs = Counter(); boxes = Counter(); raiders = Counter()
for f in fs:
    bsw = f.t[f.E].npc[f.boss].after
    sx = 1 if bsw[0] > MID[0] else -1; sz = 1 if bsw[1] > MID[1] else -1
    for t in range(f.E, (f.K or f.last)):
        T = f.t.get(t)
        if not T: continue
        for i, n in T.npc.items():
            if 'bombportal' in n.type and n.move == 'add':
                o = (sx * (n.after[0] - MID[0]), sz * (n.after[1] - MID[1]))
                offs[('rift', min(o))] += 1; boxes[('rift', oc_p4.box(bsw, 3, n.after))] += 1
        for nm, p in T.pl.items():
            if nm in f.raiders:
                o = (sx * (p.after[0] - MID[0]), sz * (p.after[1] - MID[1]))
                raiders[(min(o), max(o))] += 1
                if any(s[1] == 'vfx_ocellus_soul_fragment_player_regain' for s in p.spots):
                    o = (sx * (p.before[0] - MID[0]), sz * (p.before[1] - MID[1]))
                    offs[('orb', min(o), max(o))] += 1
print('boss sw offsets', set((f.t[f.E].npc[f.boss].after[0]-MID[0], f.t[f.E].npc[f.boss].after[1]-MID[1]) for f in fs))
print(sorted(offs.items())); print(sorted(boxes.items()))
print('raider min offset', sorted(Counter(k[0] for k in raiders.elements()).items()), 'max', sorted(Counter(k[1] for k in raiders.elements()).items()))
