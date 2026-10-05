#!/usr/bin/env python3
"""comp.py sx sz tx tz margin [level] [--allow-op-locs] -- the walking component of (sx, sz): a 4-way flood
with every door closed and every op loc / zone-trigger tile blocked (reach.py's rules). Prints its size,
the three tiles closest to the target, its max x, and the doors and op locs on its edge: one of those is
the click that leaves it."""
import os
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach  # noqa: E402

allow = "--allow-op-locs" in sys.argv
args = [a for a in sys.argv[1:] if a != "--allow-op-locs"]
if len(args) < 5:
    sys.exit(__doc__)
sx, sz, tx, tz, mg = map(int, args[:5])
level = int(args[5]) if len(args) > 5 else 0
a = reach.Area(min(sx, tx) - mg, max(sx, tx) + mg, min(sz, tz) - mg, max(sz, tz) + mg, level, allow)
seen = {(sx, sz)}
q = deque([(sx, sz)])
doors, ops = {}, {}
while q:
    c = q.popleft()
    for side, (dx, dz) in reach.DX.items():
        n = (c[0] + dx, c[1] + dz)
        if n in seen:
            continue
        if (c[0], c[1], side) in a.doorw and n not in seen:
            doors[a.doorw[(c[0], c[1], side)]] = True
        if n in a.optile and n not in a.full:
            ops[a.optile[n]] = True
    for n, _, _ in a.steps(c, {(sx, sz)}, False, False):
        if n not in seen:
            seen.add(n)
            q.append(n)
best = sorted(seen, key=lambda c: abs(c[0] - tx) + abs(c[1] - tz))[:3]
print("component size", len(seen), "closest to target", best, "maxx", max(c[0] for c in seen))
print("doors on edge:", ",".join(list(doors)[:8]) or "none", "| op locs on edge:", ",".join(list(ops)[:8]) or "none")
