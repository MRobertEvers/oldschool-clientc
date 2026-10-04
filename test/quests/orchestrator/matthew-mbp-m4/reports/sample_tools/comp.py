import sys, runpy
sys.argv = ["reach.py", sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], "0", sys.argv[5]]
src = open("reach.py").read().split("def crossed")[0]
g = {"__name__": "x", "__file__": __file__}
exec(compile(src, "reach.py", "exec"), g)
from collections import deque
sx, sz, tx, tz = g["sx"], g["sz"], g["tx"], g["tz"]
seen = {(sx, sz)}; q = deque([(sx, sz)])
while q:
    x, z = q.popleft()
    for side, (dx, dz) in g["DX"].items():
        n = (x+dx, z+dz)
        if n in seen or not (g["x0"] <= n[0] <= g["x1"] and g["z0"] <= n[1] <= g["z1"]): continue
        if side in g["wall"].get((x, z), ()): continue
        if (x, z, side) in g["doorw"]: continue
        if n in g["full"]: continue
        seen.add(n); q.append(n)
best = sorted(seen, key=lambda c: abs(c[0]-tx)+abs(c[1]-tz))[:3]
print("component size", len(seen), "closest to target", best, "maxx", max(c[0] for c in seen))
