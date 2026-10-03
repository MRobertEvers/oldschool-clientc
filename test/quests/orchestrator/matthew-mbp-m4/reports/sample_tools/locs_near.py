#!/usr/bin/env python3
"""locs_near.py x z [radius] [level] [filter-regex] -- list map locs near a world tile with names/ops."""
import os, re, sys, pickle
BASE = "/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs239-content"
CACHE = "/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/sample_b56/locs.pickle"

def load():
    if os.path.exists(CACHE):
        return pickle.load(open(CACHE, "rb"))
    ids = {}
    for line in open(BASE + "/configs/all.loc.compack"):
        if "=" in line:
            a, b = line.strip().split("=", 1)
            if a.isdigit():
                ids[int(a)] = b
    info = {}
    cur = None
    for line in open(BASE + "/configs/all.loc", errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"^\[(.+)\]$", line)
        if m:
            cur = m.group(1); info[cur] = {}
            continue
        if cur and "=" in line:
            k, v = line.split("=", 1)
            if k in ("name", "op1", "op2", "op3", "blockwalk", "width", "length", "multiloc", "multivar"):
                info[cur][k] = v
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    pickle.dump((ids, info), open(CACHE, "wb"))
    return ids, info

def main():
    x, z = int(sys.argv[1]), int(sys.argv[2])
    rad = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    lvl = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    flt = re.compile(sys.argv[5], re.I) if len(sys.argv) > 5 else None
    ids, info = load()
    seen = set()
    for mx in range((x - rad) >> 6, ((x + rad) >> 6) + 1):
        for mz in range((z - rad) >> 6, ((z + rad) >> 6) + 1):
            p = "%s/maps/m%d_%d.jl2" % (BASE, mx, mz)
            if not os.path.exists(p):
                continue
            for line in open(p):
                m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
                if not m:
                    continue
                L, lx, lz, lid, shape, rot = int(m[1]), int(m[2]), int(m[3]), int(m[4]), int(m[5]), int(m[6] or 0)
                wx, wz = mx * 64 + lx, mz * 64 + lz
                if L != lvl or abs(wx - x) > rad or abs(wz - z) > rad:
                    continue
                if shape == 22:
                    continue
                nm = ids.get(lid, str(lid))
                i = info.get(nm, {})
                s = "%d,%d s%d r%d %s '%s' %s %s" % (wx, wz, shape, rot, nm, i.get("name", ""), i.get("op1", ""), i.get("op2", ""))
                if flt and not flt.search(s):
                    continue
                if s in seen:
                    continue
                seen.add(s)
                print(s)

main()
