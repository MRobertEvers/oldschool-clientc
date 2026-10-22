# Ocellus phase 4 (oc_p4.py): every live raider-tick on the void as the tick
# began (his ring D+0..21, the rest D+31..51) and whether it took a 10.
# Run from this directory: python3 oc_p4_voidrate.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
c = Counter(); rows = []
for f in oc_p4.load():
    cur = {}; pre = {}
    for t in sorted(f.t):
        for n in f.raiders:
            p = f.t[t].pl.get(n); pre[(t, n)] = p.before if p else cur.get(n)
            if p: cur[n] = p.after
    dead = set()
    for D in f.D:
        for d in range(0, 60):
            t = D + d
            if f.K and t >= f.K: break
            T = f.t.get(t)
            if not T: continue
            b = None
            for u in range(t, t - 6, -1):
                bb = f.t.get(u) and f.t[u].npc.get(f.boss)
                if bb: b = bb.after; break
            for n in f.raiders:
                p = T.pl.get(n)
                if p and any(s[0] == 'human_death' for s in p.seqs): dead.add(n)
                tile = pre.get((t, n))
                if n in dead or tile is None or b is None or tile == (4097, 5633): continue
                dist = oc_p4.box(b, 3, tile)
                ring = d <= 21 and dist <= 1
                outer = 31 <= d <= 51 and dist >= 2 and dist <= 6
                if not (ring or outer): continue
                ten = any(ty in (16, 17) and v == 10 for ty, v in (p.hits if p else []))
                c[('ring' if ring else 'outer', d, ten)] += 1
                if ten: rows.append((f.sid, t, d, n[:8], dist))
for kind in ('ring', 'outer'):
    print(kind, ' '.join('D+%d:%d/%d' % (d, c[(kind, d, True)], c[(kind, d, True)] + c[(kind, d, False)]) for d in range(0, 52) if c[(kind, d, True)] + c[(kind, d, False)]))
print(rows)
