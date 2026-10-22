#!/usr/bin/env python3
"""Where a dying jelly's blood goes: deaths grouped by their tile and delete tick, the
blood round them, and per lone death the likelihood of each placement rule (own tile,
then one floor neighbour, each skipped on blood; and the alternatives).

    python3 dn_jellyblood_fit.py <all-players txt>...
"""
import sys, re
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P
from collections import Counter, defaultdict
DEL = re.compile(r"^ {8}\[del\] npc=\(index=(\d+), id=tfa_jelly_minion \(\d+\), coord=\((\d+), (\d+)")
HB0 = "[headbar] id=0, fill=0"
res = Counter(); rows = []; EV = []
for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
    tag = path[-8:-4]
    s = P.parse(path)
    # each jelly: death seq first tick (jelly_death), del tick and tile
    seqt = {}; dels = []
    tick = None; cur = None
    for line in open(path, errors="replace"):
        m = P.TICK.match(line.rstrip()); 
        if m: tick = int(m.group(1)); continue
        m = DEL.match(line)
        if m: dels.append((tick, m.group(1), (int(m.group(2)), int(m.group(3))))); continue
        m = re.match(r"^ {8}\[\w+\] npc=\(index=(\d+), id=tfa_jelly_minion", line)
        if m: cur = m.group(1); continue
        if line.startswith("        [") or line.startswith("    ["): cur = None
        if cur and "jelly_death" in line: seqt.setdefault(cur, tick)
    slam, land, p2 = {}, {}, defaultdict(set)
    for t in s.ticks:
        if t in s.boss and "dreadnoth_jump01" in s.boss[t][2]: slam[t + 1] = s.boss[t][1]
        if t in s.boss and "dreadnoth_jump02" in s.boss[t][2]: land[t + 3] = s.boss[t][1]
    for t, tiles in s.proj2.items():
        for x in tiles: p2[t + 6].add(x)
    unexp = []
    blood = set(); prior = {}
    for t in s.ticks:
        if t in s.boss_adds: blood = set()
        prior[t] = set(blood)
        for x in s.blood_add.get(t, ()):
            ok = False
            for d in (slam, land):
                if t in d and d[t][0] - 1 <= x[0] <= d[t][0] + 5 and d[t][1] - 1 <= x[1] <= d[t][1] + 5: ok = True
            if x in p2.get(t, ()): ok = True
            if not ok: unexp.append((t, x))
        blood |= s.blood_add.get(t, set())
    # the death tick D: del - 3 is the seq tick; blood at D = seq tick - lag(0..2). Use blood within del-5..del-2.
    groups = defaultdict(list)
    for dt, idx, x in dels:
        groups[(x, dt)].append(idx)
    used = set()
    for (x, dt), idxs in sorted(groups.items(), key=lambda kv: kv[0][1]):
        n = len(idxs)
        win = [(i, t, y) for i, (t, y) in enumerate(unexp) if i not in used and dt - 5 <= t <= dt - 3 + 0 and max(abs(y[0] - x[0]), abs(y[1] - x[1])) <= 1]
        D = min([t for _, t, _ in win] or [dt - 3])
        own_free = x not in prior.get(D, set())
        nb = len(win)
        for i, _, _ in win: used.add(i)
        own_got = any(y == x for _, _, y in win)
        res[(n, "own free" if own_free else "own blood", nb, "own got" if own_got else "-")] += 1
        if n == 1:
            pr = prior.get(D, set())
            nbrs = [(x[0]+dx, x[1]+dz) for dx in (-1,0,1) for dz in (-1,0,1) if (dx,dz) != (0,0)]
            walk = lambda y: 4184 <= y[0] <= 4198 and 5656 <= y[1] <= 5670
            freen = sum(1 for y in nbrs if walk(y) and y not in pr)
            EV.append((own_free, own_got, nb, freen, sum(1 for y in nbrs if walk(y))))
for k in sorted(res): print(k, res[k])

import math
def lik(model, own_free, own_got, nb, freen, walkn):
    # probability of (own_got, nb) under each model
    if model == "F own + 1 of 8 nbrs":
        pa = 1 if own_free else 0
        pb = freen / 8
    elif model == "F2 own + 1 of walkable nbrs":
        pa = 1 if own_free else 0
        pb = freen / max(walkn, 1)
    elif model == "G own + 1 of 3x3":
        pa = 1 if own_free else 0
        pb = freen / 9
    elif model == "G2 own + 1 of walkable 3x3":
        pa = 1 if own_free else 0
        pb = freen / (walkn + 1)
    elif model == "K own, else 1 nbr":
        pa = 1 if own_free else 0
        pb = 0 if own_free else freen / 8
    elif model == "L own + 1 of free nbrs (always)":
        pa = 1 if own_free else 0
        pb = 1 if freen else 0
    want_own = 1 if own_got else 0
    want_b = nb - want_own
    if want_b not in (0, 1): return 1e-3
    p = (pa if want_own else 1 - pa) * (pb if want_b else 1 - pb)
    return max(p, 1e-3)
for m in ["G2 own + 1 of walkable 3x3", "F own + 1 of 8 nbrs", "F2 own + 1 of walkable nbrs", "G own + 1 of 3x3", "K own, else 1 nbr", "L own + 1 of free nbrs (always)"]:
    ll = sum(math.log(lik(m, *e)) for e in EV)
    print("%-34s loglik %.1f over %d single deaths" % (m, ll, len(EV)))
