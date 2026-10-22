# Ocellus phase 4 (oc_p4.py): an orb is taken on the tile the tick began on (pre) or the one moved to (post).
# Run from this directory: python3 oc_p4_orbtick.py
import sys; sys.path.insert(0, '.')
import oc_p4
from collections import Counter
fs = oc_p4.load()
pre_ok = Counter(); post_ok = Counter()
for f in fs:
    cur = {}; pre = {}; post = {}
    for t in sorted(f.t):
        for n in f.raiders:
            p = f.t[t].pl.get(n)
            pre[(t, n)] = p.before if p else cur.get(n)
            post[(t, n)] = p.after if p else cur.get(n)
            if p: cur[n] = p.after
    for D in f.D:
        for W, ready in ((D + 4, D + 16), (D + 35, D + 41)):
            for n in f.raiders:
                for r in range(ready, ready + 13):
                    p = f.t.get(r) and f.t[r].pl.get(n)
                    if p and any(s[1] == 'vfx_ocellus_soul_fragment_player_regain' for s in p.spots):
                        # model pre: tile = pre(r); arrival = first tick a with post(a)==tile through r-1
                        for model, tile in (('pre', pre[(r, n)]), ('post', post[(r, n)])):
                            a = r if model == 'post' else r - 1
                            while post.get((a - 1, n)) == tile and a - 1 >= W: a -= 1
                            expect = max(a + (1 if model == 'pre' else 0), ready)
                            (pre_ok if model == 'pre' else post_ok)[r - expect] += 1
                        break
print('pre model, regain - expected:', dict(pre_ok))
print('post model, regain - expected:', dict(post_ok))
