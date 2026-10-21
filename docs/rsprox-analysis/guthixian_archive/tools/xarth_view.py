"""Xarth's room, tile by tile, relative to his spawn south-west tile (4190,5534).

  python3 xarth_view.py idx4301.txt 617 844 --grid 730        the map anims of one tick as a grid (delay digit)
  python3 xarth_view.py idx4301.txt 617 844 --locs            every loc add/del with offset and tick
  python3 xarth_view.py idx4301.txt 617 844 --npc tfa_xarth_corpse   one npc type's lives: add, moves, death
"""
import re, sys
from collections import defaultdict
path, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mode = sys.argv[4] if len(sys.argv) > 4 else '--locs'
arg = sys.argv[5] if len(sys.argv) > 5 else None
SW = (4190, 5534)
rel = lambda x, z: (x - SW[0], z - SW[1])
lines = []
tick = None
for line in open(path):
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        tick = int(m.group(1))
        continue
    if tick is not None and t0 <= tick <= t1:
        lines.append((tick, line.rstrip()))

if mode == '--grid':
    want = int(arg)
    cells = {}
    for t, l in lines:
        if t != want:
            continue
        m = re.search(r"\[map_anim\] id=(\w+) \(\d+\)(?:, delay=(\d+))?.*coord=\((\d+), (\d+)", l)
        if m:
            cells[rel(int(m.group(3)), int(m.group(4)))] = (m.group(1), int(m.group(2) or 0))
        m = re.search(r"loc_add_change\S* id=(\w+).*coord=\((\d+), (\d+), \d\), shape=(\d+), rotation=(\d)", l)
        if m:
            cells.setdefault(rel(int(m.group(2)), int(m.group(3))), (m.group(1), -1))
    xs = [c[0] for c in cells]; zs = [c[1] for c in cells]
    print(f"tick {want}: {len(cells)} tiles, x {min(xs)}..{max(xs)}, z {min(zs)}..{max(zs)}; digit = delay, L = loc, . = none")
    for z in range(max(zs), min(zs) - 1, -1):
        row = ''
        for x in range(min(xs), max(xs) + 1):
            c = cells.get((x, z))
            row += '.' if c is None else ('L' if c[1] < 0 else str(c[1] % 10))
        print(f"{z:4d} {row}")
elif mode == '--locs':
    for t, l in lines:
        m = re.search(r"\[(loc_add_change\S*|loc_del)\] id=(\w+).*coord=\((\d+), (\d+), \d\)(?:, shape=(\d+), rotation=(\d))?", l)
        if m:
            print(t, m.group(1)[:7], m.group(2), rel(int(m.group(3)), int(m.group(4))), f"rot={m.group(6)}")
elif mode == '--npc':
    cur = None
    for t, l in lines:
        m = re.search(r"\[(\w+)\] npc=\(index=(\d+), id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?(.*)", l)
        if m:
            cur = (m.group(3), m.group(2))
            if m.group(3) == arg and m.group(1) != 'idle':
                to = f" -> {rel(int(m.group(6)), int(m.group(7)))}" if m.group(6) else ''
                print(t, m.group(2), m.group(1), rel(int(m.group(4)), int(m.group(5))), to, m.group(8)[:60])
            continue
        if cur and cur[0] == arg and re.search(r"\[(sequence|spotanim|say|hit|headbar|transformation|enabled_ops|facing)\]", l):
            print(t, cur[1], '   ', l.strip()[:120])
