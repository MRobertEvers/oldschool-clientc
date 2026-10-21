"""Ocellus's arena (plane 1), relative to its middle (4127,5673).

  python3 ocellus_view.py idx4301.txt --grid T0 T1 [regex]   map anims (and locs) of ticks T0..T1 on the arena:
                                                            a letter per anim name (legend printed), digit-free
  python3 ocellus_view.py idx4301.txt --list T0 T1 [regex]  every map anim / loc / map projectile, offset and delay
  python3 ocellus_view.py idx4301.txt --boss T0 T1          Ocellus's tile, seqs, spots, says, facing per tick
"""
import re, sys
MID = (4127, 5673)
path, mode, t0, t1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
pat = re.compile(sys.argv[5]) if len(sys.argv) > 5 else None
rel = lambda x, z: (x - MID[0], z - MID[1])
lines = []
tick = None
for line in open(path):
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        tick = int(m.group(1))
        continue
    if tick is not None and t0 <= tick <= t1:
        lines.append((tick, line.rstrip()))

ANIM = re.compile(r"\[map_anim\] id=(\w+) \(\d+\)(?:, delay=(\d+))?(?:, height=\d+)?, coord=\((\d+), (\d+), \d\)")
LOC = re.compile(r"\[(loc_add_change\S*|loc_del)\] id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)(?:, shape=(\d+), rotation=(\d))?")
if mode == '--list':
    pend = None
    for t, l in lines:
        m = ANIM.search(l)
        if m and (not pat or pat.search(m.group(1))):
            print(t, 'anim', m.group(1), rel(int(m.group(3)), int(m.group(4))), 'd=' + (m.group(2) or '0'))
        m = LOC.search(l)
        if m and (not pat or pat.search(m.group(2))):
            print(t, m.group(1)[:7], m.group(2), rel(int(m.group(3)), int(m.group(4))), f"shape={m.group(5)} rot={m.group(6)}")
        m = re.search(r"\[map_projanim\S*\] id=(\w+) \(\d+\), (.*)", l)
        if m and (not pat or pat.search(m.group(1))):
            pend = (t, m.group(1), m.group(2))
            continue
        if pend:
            m = re.search(r"\[(source|target)\] coord=\((\d+), (\d+)", l)
            if m:
                pend = pend + ((m.group(1), rel(int(m.group(2)), int(m.group(3)))),)
                if m.group(1) == 'target':
                    print(pend[0], 'proj', pend[1], pend[3:], pend[2])
                    pend = None
elif mode == '--grid':
    cells = {}
    names = {}
    for t, l in lines:
        m = ANIM.search(l)
        if m and (not pat or pat.search(m.group(1))):
            n = m.group(1)
            if n not in names:
                names[n] = chr(ord('a') + len(names)) if len(names) < 26 else '?'
            cells[rel(int(m.group(3)), int(m.group(4)))] = names[n]
        m = LOC.search(l)
        if m and (not pat or pat.search(m.group(2))):
            n = m.group(2)
            if n not in names:
                names[n] = chr(ord('A') + len(names) % 26)
            cells[rel(int(m.group(3)), int(m.group(4)))] = names[n]
    for n, c in names.items():
        print(f"  {c} = {n}")
    for z in range(11, -12, -1):
        print(f"{z:4d} " + ''.join(cells.get((x, z), '.') for x in range(-11, 12)))
    print("     " + ''.join(str(abs(x) % 10) for x in range(-11, 12)))
elif mode == '--boss':
    cur = False
    for t, l in lines:
        m = re.search(r"^\s+\[(add|idle|walk|run|crawl|teleport|telejump|del)\] npc=\(index=\d+, id=(tfa_ocellus\w*) \(\d+\)(?:, multinpc=\S+ \(\d+\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?", l)
        if m:
            cur = True
            if m.group(1) not in ('idle',):
                to = f" -> {rel(int(m.group(5)), int(m.group(6)))}" if m.group(5) else ''
                print(t, m.group(1), m.group(2), rel(int(m.group(3)), int(m.group(4))), to)
            continue
        if re.search(r"^\s+\[(add|idle|walk|run|crawl|teleport|telejump|del)\] (npc|player)=", l):
            cur = False
            continue
        if cur and re.search(r"\[(sequence|spotanim|say|transformation|exact_move|teleport|facing\] coord)", l):
            print(t, '    ', l.strip()[:140])
