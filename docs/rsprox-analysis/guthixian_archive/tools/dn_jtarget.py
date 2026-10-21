"""Each jelly's first [facing] player: distance rank of that raider among all raiders
at the start of the tick (Chebyshev from the jelly), and whether it ever switches."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+), name=([^,]+)')
ranks = collections.Counter(); sw = collections.Counter(); per = collections.Counter(); verbose = '-v' in sys.argv
for path in [p for p in sys.argv[1:] if not p.startswith('-')]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; start = {}; pos = {}; names = {}; cur = None; jt = {}; jpos = {}; boss_t = None; inb = False; hatch_group = collections.defaultdict(list)
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); start = dict(pos); cur = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); names[idx] = m.group(3); cur = None
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            start.setdefault(idx, c)
            if m.group(1) == 'del': pos.pop(idx, None)
            else: pos[idx] = n
            continue
        m = NPC.match(line)
        if m:
            sym = m.group(3); idx = int(m.group(2)); inb = sym == 'tfa_jelly_boss'
            cur = idx if sym == 'tfa_jelly_minion' else None
            if cur: jpos[idx] = (int(m.group(5)), int(m.group(6)))
            if cur and m.group(1) == 'add': jt.pop(idx, None)
            continue
        m = FACEP.match(line)
        if m and inb: boss_t = int(m.group(1)); continue
        if m and cur:
            t = int(m.group(1))
            if cur not in jt:
                jp = jpos[cur]
                ds = sorted((max(abs(p[0]-jp[0]), abs(p[1]-jp[1])), i) for i, p in start.items() if abs(p[0]-jp[0]) < 30)
                dmin = ds[0][0] if ds else 0
                d = dict((i, dd) for dd, i in ds).get(t)
                rank = 'nearest' if d == dmin else f'+{d - dmin}'
                ranks[rank] += 1
                if t == boss_t: ranks['= boss target'] += 1
                per[names[t]] += 1
                hatch_group[tick].append(names[t])
            elif jt[cur] != t:
                jp = jpos[cur]
                ds = {i: max(abs(p[0]-jp[0]), abs(p[1]-jp[1])) for i, p in start.items() if abs(p[0]-jp[0]) < 30}
                old = jt[cur]; dmin = min(ds.values()) if ds else 0
                sw['to nearest' if ds.get(t) == dmin else 'to not-nearest'] += 1
                sw['old target gone' if old not in start else ('old dead/far' if ds.get(old, 99) > dmin else 'old tied nearest')] += 1
            jt[cur] = t
    if verbose:
        for t, ws in sorted(hatch_group.items()): print(sess, t, collections.Counter(ws))
print(ranks, sw)
