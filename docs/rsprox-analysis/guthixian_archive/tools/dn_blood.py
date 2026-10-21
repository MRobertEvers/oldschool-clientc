"""Hits on each player while their tile holds a Dreadnoth blood tile.

  python3 dn_blood.py idx4301.txt 307 549   (needs an --all-players transcript)
"""
import re, sys
path, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
tick = None
blood = set()
pos = {}
cur = None
rows = []
for line in open(path):
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        tick = int(m.group(1))
        continue
    if tick is None or tick < t0 or tick > t1:
        continue
    m = re.search(r"loc_add_change\S* .*id=tfa_jelly_boss_damage_tile.*coord=\((\d+), (\d+)", line)
    if m:
        blood.add((int(m.group(1)), int(m.group(2))))
        continue
    m = re.search(r"\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?", line)
    if m:
        name = m.group(2)
        x, z = (int(m.group(5)), int(m.group(6))) if m.group(5) else (int(m.group(3)), int(m.group(4)))
        pos[name] = (x, z)
        cur = name
        continue
    if re.search(r"\] npc=", line):
        cur = None
        continue
    m = re.search(r"\[hit\] id=(\d+), value=(\d+)", line)
    if m and cur:
        on = pos.get(cur) in blood
        rows.append((tick, cur, int(m.group(1)), int(m.group(2)), on, pos.get(cur)))
on = [r for r in rows if r[4]]
print(f"{len(blood)} blood tiles; {len(rows)} player hits, {len(on)} on blood")
for r in on[:60]:
    print(*r)
