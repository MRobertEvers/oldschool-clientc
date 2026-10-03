#!/usr/bin/env python3
"""reach.py sx sz tx tz [level] [margin] -- 4-dir BFS over static map collision (jm2 floor flags + jl2 locs).
Reports: reachable with doors closed? if not, reachable with doors/gates opened, and which doors were crossed.
Ignores script-spawned locs, npcs, diagonal-only gaps, and maplinks (stairs)."""
import os, re, sys, pickle
from collections import deque
BASE = "/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs239-content"
# The loc-name table is a 5 MB pickle that is not committed: use a copy beside this script, else
# locs_near.py's cache under build/, building it there (one locs_near.py call) when neither exists.
HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = "/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/sample_b56/locs.pickle"
PICKLE = HERE + "/locs.pickle" if os.path.exists(HERE + "/locs.pickle") else SHARED
if not os.path.exists(PICKLE):
    import subprocess
    subprocess.run([sys.executable, HERE + "/locs_near.py", "0", "0", "0"], check=True, capture_output=True)
ids, info = pickle.load(open(PICKLE, "rb"))
sx, sz, tx, tz = map(int, sys.argv[1:5])
P = int(sys.argv[5]) if len(sys.argv) > 5 else 0
MG = int(sys.argv[6]) if len(sys.argv) > 6 else 40
x0, x1 = min(sx, tx) - MG, max(sx, tx) + MG
z0, z1 = min(sz, tz) - MG, max(sz, tz) + MG
flags = {}   # (L,x,z) -> floor flag
locs = []
for mx in range(x0 >> 6, (x1 >> 6) + 1):
    for mz in range(z0 >> 6, (z1 >> 6) + 1):
        p = "%s/maps/m%d_%d" % (BASE, mx, mz)
        if os.path.exists(p + ".jm2"):
            for line in open(p + ".jm2"):
                m = re.match(r"^(\d) (\d+) (\d+):(.*)$", line)
                if m:
                    f = re.search(r"\bf(\d+)", m[4])
                    if f:
                        flags[(int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]))] = int(f[1])
        if os.path.exists(p + ".jl2"):
            for line in open(p + ".jl2"):
                m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
                if m:
                    locs.append((int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]), int(m[4]), int(m[5]), int(m[6] or 0)))

def eff(L, x, z):
    return L - 1 if flags.get((1, x, z), 0) & 2 else L

full = set()
wall = {}      # (x,z) -> set of sides blocked  0=W 1=N 2=E 3=S
doorw = {}     # (x,z,side) -> door name
for (L, x, z), f in flags.items():
    if f & 1 and eff(L, x, z) == P:
        full.add((x, z))
DX = {0: (-1, 0), 1: (0, 1), 2: (1, 0), 3: (0, -1)}
def addwall(x, z, side, door):
    dx, dz = DX[side]
    for (a, b, s) in ((x, z, side), (x + dx, z + dz, (side + 2) % 4)):
        if door:
            doorw[(a, b, s)] = door
        else:
            wall.setdefault((a, b), set()).add(s)
for (L, x, z, lid, shape, rot) in locs:
    if eff(L, x, z) != P:
        continue
    nm = ids.get(lid, str(lid)); i = info.get(nm, {})
    if i.get("blockwalk") == "0":
        continue
    op = (i.get("op1", "") + i.get("op2", "")).lower()
    isdoor = ("open" in op or "door" in i.get("name", "").lower() or "gate" in i.get("name", "").lower()) and shape in (0, 2)
    door = ("%s@%d,%d" % (nm, x, z)) if isdoor else None
    if shape == 0:
        addwall(x, z, rot, door)
    elif shape == 2:
        addwall(x, z, rot, door); addwall(x, z, (rot + 1) % 4, door)
    elif shape == 9:
        full.add((x, z))
    elif shape in (10, 11):
        w, l = int(i.get("width", 1)), int(i.get("length", 1))
        if rot & 1:
            w, l = l, w
        for a in range(w):
            for b in range(l):
                full.add((x + a, z + b))

def bfs(open_doors):
    prev = {(sx, sz): None}
    q = deque([(sx, sz)])
    while q:
        c = q.popleft()
        if c == (tx, tz):
            path = []
            while c:
                path.append(c); c = prev[c]
            return path[::-1]
        x, z = c
        for side, (dx, dz) in DX.items():
            n = (x + dx, z + dz)
            if n in prev or not (x0 <= n[0] <= x1 and z0 <= n[1] <= z1):
                continue
            if side in wall.get(c, ()):
                continue
            if (x, z, side) in doorw and not open_doors:
                continue
            if n in full and n != (tx, tz):
                continue
            prev[n] = c; q.append(n)
    return None

def crossed(path):
    out = []
    for a, b in zip(path, path[1:]):
        for side, (dx, dz) in DX.items():
            if (a[0] + dx, a[1] + dz) == b and (a[0], a[1], side) in doorw:
                out.append(doorw[(a[0], a[1], side)])
    return out

note = []
if (sx, sz) in full: note.append("start tile solid")
if (tx, tz) in full: note.append("target tile solid")
p = bfs(False)
if p:
    print("REACH closed-doors len=%d %s" % (len(p) - 1, " ".join(note)))
else:
    p2 = bfs(True)
    if p2:
        print("NEEDS-DOOR len=%d via %s %s" % (len(p2) - 1, ",".join(dict.fromkeys(crossed(p2))), " ".join(note)))
    else:
        print("UNREACHABLE (margin %d) %s" % (MG, " ".join(note)))
