"""Dreadnoth's freezes: each ice_barrage_impact / vfx_scorching_bow_impact_01 on him
(the cast tick, delay, its landing tick L = tick + (delay + 15) // 30), then a per-tick
strip from L-4 to L+40: c crawl, M melee, J slam, E leap, i another impact, s say,
'.' still; under it the target's box distance at the start of the tick ('a' = on a
side: he would stand anyway)."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
SPOT = re.compile(r'^ {12}\[spotanim\](?: slot=\d+,)? id=(\w+) \(\d+\)(?:, delay=(\d+))?')
FREEZE = ('ice_barrage_impact', 'vfx_scorching_bow_impact_01')
def geo(sw, p):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + 4) if v > lo + 4 else 0)
    ex, ez = f(p[0], sw[0]), f(p[1], sw[1])
    if ex + ez == 1: return 'a'
    d = max(ex, ez); return str(d) if d < 10 else '+'
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; pos = {}; start = {}; T = collections.defaultdict(dict); cur = None; tgt = None; sw = None; hits = []
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1)); start = dict(pos); cur = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); c = (int(m.group(4)), int(m.group(5)))
            n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            start.setdefault(idx, c); pos[idx] = n; cur = None; continue
        m = NPC.match(line)
        if m:
            cur = None
            if m.group(3) != 'tfa_jelly_boss': continue
            cur = 1; sw = (int(m.group(5)), int(m.group(6)))
            r = T[tick]; r.setdefault('ch', '.')
            if m.group(1) == 'crawl': r['ch'] = 'c'
            if m.group(1) == 'teleport': r['ch'] = 't'
            if m.group(1) == 'del': r['ch'] = 'X'
            r['sw'] = sw; continue
        if not cur: continue
        r = T[tick]
        m = FACEP.match(line)
        if m: tgt = int(m.group(1)); continue
        m = SEQ.match(line)
        if m: r['ch'] = {'dreadnoth_melee01': 'M', 'dreadnoth_jump01': 'J', 'dreadnoth_jump02': 'E', 'dreadnoth_death01': 'D'}.get(m.group(1), r['ch']); continue
        if '[say]' in line: r['ch'] = 's'; continue
        m = SPOT.match(line)
        if m and m.group(1) in FREEZE:
            d = int(m.group(2) or 0); hits.append((tick, m.group(1), d, tick + (d + 15) // 30))
        if tgt is not None and tgt in start and sw: r['g'] = geo(sw, start[tgt])
    for t, name, d, L in hits:
        s1 = ''; s2 = ''
        for k in range(L - 4, L + 41):
            r = T.get(k, {}); ch = r.get('ch', ' ')
            if any(h[3] == k for h in hits) and ch == '.': ch = 'i'
            s1 += ch; s2 += r.get('g', ' ')
        print(f"{sess} cast [{t}] {name[:12]} d={d} lands [{L}]  {s1}")
        print(f"{' ' * (len(sess) + 37 + len(str(t)) + len(str(L)) + len(name[:12]) + len(str(d)) - 4)}{s2}")
