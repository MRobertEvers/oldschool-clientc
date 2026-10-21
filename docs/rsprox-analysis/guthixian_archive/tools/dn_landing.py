"""Dreadnoth's enrage landings: per jump02 at J, the tick J+3: hits (by who),
his heal splat (id 6), blood tiles relative to his new south-west tile, and
the proj02 tiles with what lands on them at J+9.

  python3 dn_landing.py idx4303.txt 2421 3187
"""
import re, sys
path, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ticks = {}
tick = None
for line in open(path):
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        tick = int(m.group(1))
        continue
    if tick is not None and t0 <= tick <= t1:
        ticks.setdefault(tick, []).append(line.rstrip())
jumps = [t for t, ls in ticks.items() if any("dreadnoth_jump02" in l for l in ls)]
for j in sorted(jumps):
    sw = None
    for l in ticks.get(j, []):
        m = re.search(r"\[teleport\] npc=.*tfa_jelly_boss.*newcoord=\((\d+), (\d+)", l)
        if m:
            sw = (int(m.group(1)), int(m.group(2)))
    land = ticks.get(j + 3, [])
    who = None
    hits = []
    for l in land:
        m = re.search(r"\] (player|npc)=\(index=\d+, (?:name=([^,]+)|id=(\w+))", l)
        if m:
            who = m.group(2) or m.group(3)
        m = re.search(r"\[hit\] id=(\d+), value=(\d+)", l)
        if m and m.group(1) in ("6", "16", "17"):
            hits.append((who, int(m.group(1)), int(m.group(2))))
    blood = []
    p2 = []
    for l in land:
        m = re.search(r"damage_tile.*coord=\((\d+), (\d+)", l)
        if m and sw:
            blood.append((int(m.group(1)) - sw[0], int(m.group(2)) - sw[1]))
    later = [l for l in ticks.get(j + 9, []) if "damage_tile" in l]
    print(f"J={j} sw={sw} hits={hits} blood@J+3={blood} blood@J+9={len(later)}")
