#!/usr/bin/env python3
"""Dreadnoth's blood damage: over every raider-tick in his room, the hits the recorded
rule predicts (hit on the first tick a raider starts on blood, then every second tick
they still start on it) and ours before (armed on the first tick, hits from the next),
against the 7/8 splats on the wire.

    python3 dn_bloodrule.py <all-players txt>...
"""
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P
from collections import Counter
res = {m: Counter() for m in ("recorded_rule", "ours")}
miss = []
for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
    s = P.parse(path); pos = P.positions(s); tag = path[-8:-4]
    added = set(); prevb = set()
    st = {m: {} for m in res}
    for t in s.ticks:
        if t in s.boss_adds:
            added, prevb = set(), set(); st = {m: {} for m in res}
        added |= s.blood_add.get(t, set())
        now = {x for x in added if not (x in s.blood_gone and s.blood_gone[x] <= t)}
        for name, (a, b) in pos[t].items():
            if not (a[0] // 64 == 65 and a[1] // 64 == 88):
                continue
            on = a in prevb
            got = any(v in (7, 8) for v in P.hits_of(s, t, name))
            for m in res:
                d = st[m]
                hit = False
                if m == "recorded_rule":
                    if not on: d.pop(name, None)
                    elif name not in d or t >= d[name]:
                        hit = True; d[name] = t + 2
                else:  # ours: arm on the first tick seen on, hit from the next, every 2
                    if not on: d.pop(name, None)
                    elif name not in d: d[name] = t + 1
                    elif t >= d[name]:
                        hit = True; d[name] = t + 2
                res[m][(hit, got)] += 1
                if m == "recorded_rule" and hit != got and (on or got):
                    miss.append((tag, t, name, a, b, on, P.hits_of(s, t, name)))
        prevb = now
for m, c in res.items():
    print(m, "predicted&hit %d, predicted&none %d, unpredicted 7/8 %d" % (c[(True, True)], c[(True, False)], c[(False, True)]))
for x in miss: print("  ", x)

# Blood new on tick T under a raider who started T on that tile: when is the
# first 7/8? (stayed: T+1; stepped off on T: never)
first = Counter()
for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
    s = P.parse(path); pos = P.positions(s)
    for T in s.ticks:
        for x in s.blood_add.get(T, ()):
            for name, (a, b) in pos[T].items():
                if a != x:
                    continue
                k = None
                for d in range(0, 4):
                    if T + d not in pos or (d > 0 and pos[T + d][name][0] != x):
                        break
                    if any(v in (7, 8) for v in P.hits_of(s, T + d, name)):
                        k = d
                        break
                first[("stepped off on T" if b != x else "stayed", "first hit T+%s" % k)] += 1
print("blood new under a raider:", sorted(first.items(), key=str))
