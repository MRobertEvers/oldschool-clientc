#!/usr/bin/env python3
"""Dreadnoth's blood at a slam (J+1) and a landing (L+3): how many tries, and drawn from
where. Per event the count of new blood tiles against the blood already down, under
M1 (a try anywhere in the 7x7 round his south-west tile, lost on a wall), M3 (a try on
the box's floor tiles) and M4 (on free tiles only), then an EM over the number of tries.

    python3 dn_boxmodels.py <all-players txt>...
"""
import sys, random, math
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P
from collections import Counter, defaultdict
random.seed(1)
def walk(x): return 4184 <= x[0] <= 4198 and 5656 <= x[1] <= 5670
def sim(model, box, blood, tries, N=600):
    c = Counter()
    boxl = sorted(box)
    for _ in range(N):
        b = set(blood); n = 0
        for _ in range(tries):
            if model == "M1": cand = boxl
            elif model == "M3": cand = [x for x in boxl if walk(x)]
            else: cand = [x for x in boxl if walk(x) and x not in b]
            if not cand: break
            x = random.choice(cand)
            if walk(x) and x not in b: b.add(x); n += 1
        c[n] += 1
    return {k: v / N for k, v in c.items()}
events = []
for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
    s = P.parse(path)
    P.positions_cache = getattr(P, 'positions_cache', {}); P.positions_cache[path] = P.positions(s)
    blood = set(); p2land = defaultdict(set)
    for t, tiles in s.proj2.items():
        for x in tiles: p2land[t + 6].add(x)
    ev = {}
    for t in s.ticks:
        if t in s.boss and "dreadnoth_jump01" in s.boss[t][2]: ev[t + 1] = ("slam", s.boss[t][1], 3)
        if t in s.boss and "dreadnoth_jump02" in s.boss[t][2]: ev[t + 3] = ("land", s.boss[t][1], 5)
    for t in s.ticks:
        if t in s.boss_adds: blood = set()
        add = s.blood_add.get(t, set())
        if t in ev:
            kind, sw, tries = ev[t]
            box = {(sw[0] + dx, sw[1] + dz) for dx in range(-1, 6) for dz in range(-1, 6)}
            mine = {x for x in add if x in box and x not in p2land.get(t, set())}
            raiders = len([n for n, (a, b) in P.positions_cache[path][t].items() if b[0] // 64 == 65 and b[1] // 64 == 88])
            events.append((kind, box, set(blood), tries, len(mine), raiders))
        blood |= add
        blood = {x for x in blood if not (x in s.blood_gone and s.blood_gone[x] <= t)}


for kind, K in (("slam", 5), ("land", 7)):
    ev = [e for e in events if e[0] == kind]
    L = [[sim("M3", e[1], e[2], k, 800).get(e[4], 0) for k in range(K + 1)] for e in ev]
    w = [1.0 / (K + 1)] * (K + 1)
    for _ in range(300):
        r = []
        for row in L:
            z = [w[k] * row[k] for k in range(K + 1)]; sz = sum(z) or 1e-12
            r.append([v / sz for v in z])
        w = [sum(x[k] for x in r) / len(r) for k in range(K + 1)]
    ll = sum(math.log(max(sum(w[k] * row[k] for k in range(K + 1)), 1e-12)) for row in L)
    print(kind, "EM tries weights", [round(x, 2) for x in w], "loglik %.1f" % ll)
for kind, tries in (("slam", 3), ("land", 5)):
    ev = [e for e in events if e[0] == kind]
    for model in ("M1", "M3", "M4"):
        ll = sum(math.log(max(sim(model, e[1], e[2], tries, 800).get(e[4], 0), 1e-4)) for e in ev)
        print(kind, model, "tries", tries, "loglik %.1f" % ll)
