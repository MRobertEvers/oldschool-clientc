"""Dreadnoth's per-tick state machine: for each tick of each life, 'M' melee,
'c' crawl, 'J' jump01, 'E' jump02, 'D' death, '.' nothing; with the min raider
box distance at the start of the tick. Prints each life as rows of 50.
Also: eligible-but-no-swing ticks (someone d<=1, >=4 since the last swing)."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
SAY = re.compile(r'^ {12}\[say\]')
SPOT = re.compile(r'^ {12}\[spotanim\] id=(ice_barrage_impact|ice_blitz_impact|.*bind.*|.*snare.*|.*entangle.*)')
def boxd(sw, p, n=5):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + n - 1) if v > lo + n - 1 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
verbose = '-v' in sys.argv
for path in [p for p in sys.argv[1:] if not p.startswith('-')]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; pos = {}; start = {}; lives = []; life = None; cur = None
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1)); start = dict(pos); cur = None
            if life is not None: life['rows'][tick] = {'ch': '.', 'd': None}
            continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); cur = None
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            start.setdefault(idx, c)
            if m.group(1) == 'del': pos.pop(idx, None)
            else: pos[idx] = n
            continue
        m = NPC.match(line)
        if m:
            if m.group(3) != 'tfa_jelly_boss': cur = None; continue
            kind = m.group(1); sw = (int(m.group(5)), int(m.group(6)))
            if kind == 'add':
                life = {'t0': tick, 'rows': {tick: {'ch': 'A', 'd': None}}}; lives.append(life)
            if life is None: continue
            r = life['rows'].setdefault(tick, {'ch': '.', 'd': None})
            ds = [boxd(sw, p) for p in start.values() if abs(p[0] - sw[0]) < 40]
            r['d'] = min(ds) if ds else None
            if kind == 'crawl': r['ch'] = 'c'
            if kind == 'teleport': r['ch'] = 't'
            if kind == 'del': r['ch'] = 'X'; life = None
            cur = r; continue
        if cur is None: continue
        m = SEQ.match(line)
        if m:
            cur['ch'] = {'dreadnoth_melee01': 'M', 'dreadnoth_jump01': 'J', 'dreadnoth_jump02': 'E', 'dreadnoth_death01': 'D'}.get(m.group(1), cur['ch']); continue
        if SAY.match(line): cur['ch'] = 'S'; continue
        if SPOT.match(line): cur['frz'] = 1
    for L in lives:
        ts = sorted(L['rows'])
        line1 = ''; line2 = ''
        for t in ts:
            r = L['rows'][t]; line1 += r['ch'] if not r.get('frz') or r['ch'] != '.' else 'f'
            d = r['d']; line2 += '-' if d is None else (str(d) if d < 10 else '+')
        print(f"== {sess} life from [{ts[0]}]")
        for i in range(0, len(ts), 60):
            print(f"  [{ts[i]:5d}] {line1[i:i+60]}")
            print(f"          {line2[i:i+60]}")
