# Ocellus phase 4 (oc_p4.py): each wind hit, the orb taking (regain spot) after it, the heal, the tile.
# Run from this directory: python3 oc_p4_orbs.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
fs = oc_p4.load()
rows = []
for f in fs:
    for D in f.D:
        for W in (D + 4, D + 35):
            if f.K and W > f.K: continue
            T = f.t.get(W)
            if not T: continue
            bsw = None
            for t in range(W - 5, W + 1):
                b = f.t[t].npc.get(f.boss)
                if b: bsw = b.after
            for n in f.raiders:
                p = T.pl.get(n)
                w = [v for ty, v in (p.hits if p else []) if ty in (16, 17)]
                if not w: continue
                w = w[0]
                took = None
                for t in range(W + 1, W + 30):
                    q = f.t.get(t, None)
                    q = q and q.pl.get(n)
                    if q and any(s[1] == 'vfx_ocellus_soul_fragment_player_regain' for s in q.spots):
                        h = [v for ty, v in q.hits if ty == 6]
                        took = (t - W, h, q.before, q.after)
                        break
                rows.append((f.sid, W, 'first' if W == D + 4 else 'second', n, w, took, bsw))
for r in rows:
    sid, W, k, n, w, took, bsw = r
    if took:
        dt, h, b, a = took
        tile = b
        rel = (tile[0] - bsw[0], tile[1] - bsw[1])
        print(sid, W, k, n, 'wind', w, 'took +%d' % dt, 'heal', h, 'diff', [x - w for x in h], 'rel', rel, 'box', oc_p4.box(bsw, 3, tile), 'after', oc_p4.box(bsw, 3, a))
    else:
        print(sid, W, k, n, 'wind', w, 'NOT TAKEN')
