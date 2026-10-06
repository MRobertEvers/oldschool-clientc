#!/usr/bin/env python3
"""locs_near.py x z [radius] [level] [filter-regex] [--root <repo>] -- list map locs near a world tile with names/ops.

--root names the checkout whose maps and configs are read (default: the checkout this file lives in,
reach.repo_of); the loc table is cached under that checkout's build/."""
import os, re, sys, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach  # noqa: E402
ARGV = reach.take_root(sys.argv[1:])
BASE = reach.BASE
CACHE = reach.REPO + "/build/orchestrator/sample_tools/locs_near.pickle"

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
    argv = [sys.argv[0]] + ARGV
    x, z = int(argv[1]), int(argv[2])
    rad = int(argv[3]) if len(argv) > 3 else 6
    lvl = int(argv[4]) if len(argv) > 4 else 0
    flt = re.compile(argv[5], re.I) if len(argv) > 5 else None
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
