"""Each jelly_attack: the offset from the jelly to its faced target at the start of
the tick (Chebyshev 0 / cardinal 1 / diagonal 1 / further), and the jelly's moves
per tick (tiles)."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
geo = collections.Counter(); mv = collections.Counter(); moved_then = collections.Counter()
for path in sys.argv[1:]:
    tick = 0; start = {}; pos = {}; cur = None; tgt = {}; jp = {}; jmoved = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); start = dict(pos); cur = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); cur = None
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            start.setdefault(idx, c); pos[idx] = n; continue
        m = NPC.match(line)
        if m:
            cur = None
            if m.group(3) != 'tfa_jelly_minion': continue
            cur = int(m.group(2)); c = (int(m.group(8)), int(m.group(9))) if m.group(8) else (int(m.group(5)), int(m.group(6)))
            jp[cur] = c; mv[m.group(1)] += 1
            if m.group(1) in ('walk', 'run'): jmoved[cur] = tick
            continue
        if cur is None: continue
        m = FACEP.match(line)
        if m: tgt[cur] = int(m.group(1)); continue
        m = SEQ.match(line)
        if m and m.group(1) == 'jelly_attack' and tgt.get(cur) in start:
            p = start[tgt[cur]]; j = jp[cur]; dx, dz = abs(p[0]-j[0]), abs(p[1]-j[1])
            g = 'under' if dx == dz == 0 else ('cardinal' if dx + dz == 1 else ('diagonal' if dx == dz == 1 else f'd{max(dx,dz)}'))
            geo[g] += 1
            moved_then['moved this tick' if jmoved.get(cur) == tick else 'still'] += 1
print(geo); print(mv); print(moved_then)
