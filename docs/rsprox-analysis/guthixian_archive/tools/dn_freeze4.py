"""Every freezing cast on Dreadnoth while he chases (no slam hold/enrage/death up to his
next step, target not on his side): its start (barrage: the cast; scorching bow: its
landing), N = his next step, HELD if N > start + 3. For each, the ticks since his last
thaw (the step that ended the last HELD freeze) and since his engage."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
SPOT = re.compile(r'^ {12}\[spotanim\](?: slot=\d+,)? id=(\w+) \(\d+\)(?:, delay=(\d+))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+)')
def side(sw, p):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + 4) if v > lo + 4 else 0)
    return f(p[0], sw[0]) + f(p[1], sw[1]) == 1
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; cur = False; crawl = []; J = []; busy = set(); casts = []; engage = None; pos = {}; start = {}; tgt = None; sw = None; sideat = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); cur = False; start = dict(pos); continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); c = (int(m.group(4)), int(m.group(5)))
            pos[idx] = (int(m.group(7)), int(m.group(8))) if m.group(7) else c; start.setdefault(idx, c); cur = False; continue
        m = NPC.match(line)
        if m:
            cur = m.group(3) == 'tfa_jelly_boss'
            if cur:
                sw = (int(m.group(5)), int(m.group(6)))
                if m.group(1) == 'crawl': crawl.append(tick)
                if m.group(1) == 'add': engage = None
                if m.group(1) in ('teleport', 'del'): busy.add(tick)
            continue
        if not cur: continue
        m = FACEP.match(line)
        if m:
            tgt = int(m.group(1))
            if engage is None: engage = tick
        if tgt in start and sw: sideat[tick] = side(sw, start[tgt])
        m = SEQ.match(line)
        if m:
            if m.group(1) == 'dreadnoth_jump01': J.append(tick)
            if m.group(1) in ('dreadnoth_jump02', 'dreadnoth_death01'): busy.update(range(tick, tick + 400))
        if '[say]' in line: busy.update(range(tick, tick + 400))
        m = SPOT.match(line)
        if m and m.group(1) in ('ice_barrage_impact', 'vfx_scorching_bow_impact_01'):
            d = int(m.group(2) or 0); casts.append((tick, m.group(1), d, tick + (d + 15) // 30, engage))
    ph = []
    for j in J:
        if ph and j - ph[-1][1] <= 7: ph[-1][1] = j
        else: ph.append([j, j])
    for a, b in ph: busy.update(range(a, b + 10))
    thaw = None
    for C, name, d, L, eng in casts:
        s0 = C if name == 'ice_barrage_impact' else L
        after = [c for c in crawl if c > s0]
        if not after: continue
        N = after[0]
        if any(k in busy for k in range(s0, N + 1)) or any(sideat.get(k) for k in range(s0, N)):
            continue
        held = N > s0 + 3
        print(f"  {sess} {name[:5]} cast [{C}] start [{s0}] next step [{N}] {'HELD  start+' + str(N - s0) if held else 'free'}  since thaw {s0 - thaw if thaw else '-'}  since engage {s0 - eng if eng else '-'}")
        if held: thaw = N
