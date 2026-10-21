"""Dreadnoth's jellies: per jelly life its add tile, swing ticks and death; the
boss's melee ticks with the jellies alive then.

  python3 dn_jelly.py idx4301.txt 307 549
"""
import re, sys
from collections import defaultdict

path, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
tick = None
cur = None  # (kind, index)
life = {}   # index -> dict
lives = []
alive = set()
melee = []
jumps = []
blob = defaultdict(list)
NPC = re.compile(r"\[(add|idle|walk|run|crawl|teleport|telejump|del|remove)\] npc=\(index=(\d+), id=(tfa_jelly_\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)")
for line in open(path):
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        tick = int(m.group(1))
        continue
    if tick is None or tick < t0 or tick > t1:
        continue
    m = NPC.search(line)
    if m:
        kind, idx, typ, x, z = m.group(1), int(m.group(2)), m.group(3), int(m.group(4)), int(m.group(5))
        cur = (typ, idx)
        if typ == "tfa_jelly_minion":
            if kind == "add":
                d = {"add": tick, "tile": (x, z), "swings": [], "death": None, "idx": idx}
                life[idx] = d
                lives.append(d)
                alive.add(idx)
            elif kind in ("del", "remove"):
                alive.discard(idx)
        continue
    if "[sequence]" in line and cur:
        if cur[0] == "tfa_jelly_minion" and cur[1] in life:
            if "jelly_attack" in line:
                life[cur[1]]["swings"].append(tick)
            elif "jelly_death" in line:
                life[cur[1]]["death"] = tick
                alive.discard(cur[1])
        elif cur[0] == "tfa_jelly_boss":
            s = re.search(r"id=(\w+)", line).group(1)
            if "melee" in s:
                melee.append((tick, len(alive)))
            elif "jump" in s or "death" in s:
                jumps.append((tick, s))
    if "jelly_spawning_idle" in line:
        c = re.search(r"coord=\((\d+), (\d+)", line)
        if c:
            blob[tick].append((int(c.group(1)), int(c.group(2))))
    if re.match(r"^\s+\[", line) is None:
        cur = None if not line.startswith("        ") else cur

print("boss seqs:", jumps)
print("melee (tick, jellies alive):", melee)
for t in sorted(blob):
    print("blob", t, sorted(set(blob[t])))
byadd = defaultdict(list)
for d in lives:
    byadd[d["add"]].append(d)
for t in sorted(byadd):
    ds = byadd[t]
    print(f"hatch {t}: {len(ds)} at", sorted(d["tile"] for d in ds))
    for d in ds:
        sw = d["swings"]
        gaps = [b - a for a, b in zip(sw, sw[1:])]
        print(f"   {d['idx']} first+{(sw[0]-t) if sw else '-'} gaps {gaps} death {d['death']}")
