"""Who Dreadnoth's melee01 hits: per swing tick T, every raider's box distance at
the start of T and their splats on T..T+1. usage: dn_melee.py <all-players txt>..."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
HIT = re.compile(r'^ {12}\[hit\] id=(\d+), value=(-?\d+)')
def boxd(sw, p, n=5):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + n - 1) if v > lo + n - 1 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
tab = collections.defaultdict(lambda: [0, 0, []])  # d -> [swings with someone at d, hit nonzero/any splat, values]
for path in sys.argv[1:]:
    tick = 0; pos = {}; names = {}; hits = collections.defaultdict(list); swings = []; cur = None; who = None; snap = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1)); snap[tick] = dict(pos); cur = None; who = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); names[idx] = m.group(3); who = idx; cur = None
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            snap[tick].setdefault(idx, c)
            if m.group(1) == 'del': pos.pop(idx, None)
            else: pos[idx] = n
            continue
        m = NPC.match(line)
        if m:
            who = None
            cur = (int(m.group(5)), int(m.group(6))) if m.group(3) == 'tfa_jelly_boss' else None; continue
        m = SEQ.match(line)
        if m and cur and m.group(1) == 'dreadnoth_melee01': swings.append((tick, cur)); continue
        m = HIT.match(line)
        if m and who is not None: hits[(tick, who)].append((int(m.group(1)), int(m.group(2))))
    for t, sw in swings:
        for idx, p in snap.get(t, {}).items():
            d = boxd(sw, p)
            if d > 3: continue
            h = hits.get((t, idx), []) + hits.get((t + 1, idx), [])
            row = tab[d]; row[0] += 1
            if h: row[1] += 1; row[2] += [v for _, v in h]
            if len(sys.argv) and '-v' in sys.argv[0:0]: pass
for d in sorted(tab):
    n, k, vs = tab[d]
    print(f"d={d}: raider-swings {n}, with a splat T..T+1 {k}, values {sorted(vs)[:40]}")
