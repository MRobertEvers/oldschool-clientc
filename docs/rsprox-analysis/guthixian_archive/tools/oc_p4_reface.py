# Ocellus phase 4 (oc_p4.py): his deflect face re-sends against the angle to
# his target's tile as the tick began: is the block sent exactly when the
# angle (2048 units, his middle to the tile) differs from the last one sent?
# Run from this directory: python3 oc_p4_reface.py
import sys, re, math; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
c = Counter(); fb = Counter()
ang = lambda dx, dz: int(round((math.degrees(math.atan2(-dx, -dz)) % 360) / 360 * 2048)) % 2048
for f in oc_p4.load():
    cur = {}; pre = {}
    for t in sorted(f.t):
        for n in f.raiders:
            p = f.t[t].pl.get(n); pre[(t, n)] = p.before if p else cur.get(n)
            if p: cur[n] = p.after
    for D in f.D:
        b0 = f.t.get(D) and f.t[D].npc.get(f.boss)
        if not b0 or not b0.faces or 'player=' not in b0.faces[0]: continue
        tgt = re.search(r'name=([^,]*),', b0.faces[0]).group(1)
        mid = (b0.after[0] + 1, b0.after[1] + 1)
        last = None
        for d in range(0, 21):
            t = D + d
            if f.K and t >= f.K: break
            T = f.t.get(t); b = T and T.npc.get(f.boss)
            tile = pre.get((t, tgt))
            if tile is None: continue
            a = ang(tile[0] - mid[0], tile[1] - mid[1]) if tile != mid else None
            sent = bool(b and b.faces and 'player=' in b.faces[0])
            if sent:
                m = re.search(r'fallbackangle=(\d+)', b.faces[0])
                fb[int(m.group(1)) == a] += 1
            if d > 0:
                c[(a != last, sent)] += 1
            if sent: last = a
print('fallbackangle == angle to the start-of-tick tile:', dict(fb))
print('(angle changed since last sent, sent):', dict(c))
