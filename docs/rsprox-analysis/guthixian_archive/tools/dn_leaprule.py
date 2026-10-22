"""The leap rule tested on every leap: need = target (start of the tick) - 2 - his sw;
within 5 on both axes he centres on the target, otherwise he jumps 5 tiles in the
nearest of the 8 directions (by the need's angle); the arena clamp after. Misses printed."""
import re, sys, math, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+)')
cl = lambda v, lo, hi: max(lo, min(hi, v))
LIM = int(sys.argv[1]); paths = sys.argv[2:]
def rule(fr, p):
    need = (p[0] - 2 - fr[0], p[1] - 2 - fr[1])
    if max(abs(need[0]), abs(need[1])) <= LIM: mv = need
    else:
        a = math.atan2(need[1], need[0]); o = round(a / (math.pi / 4)) % 8
        d = [(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)][o]
        mv = (5 * d[0], 5 * d[1])
    return (cl(fr[0] + mv[0], 4184, 4194), cl(fr[1] + mv[1], 5656, 5666))
ok = 0; n = 0; misses = []
for path in paths:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; pos = {}; hist = {}; cur = False; tgts = {}; leaps = []
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); hist[tick] = dict(pos); cur = False; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); c = (int(m.group(4)), int(m.group(5)))
            hist[tick].setdefault(idx, c); pos[idx] = (int(m.group(7)), int(m.group(8))) if m.group(7) else c; cur = False; continue
        m = NPC.match(line)
        if m:
            cur = m.group(3) == 'tfa_jelly_boss'
            if cur and m.group(1) == 'teleport': leaps.append((tick, (int(m.group(5)), int(m.group(6))), (int(m.group(8)), int(m.group(9)))))
            continue
        if cur:
            m = FACEP.match(line)
            if m: tgts[tick] = int(m.group(1))
    tg_at = lambda t: tgts[max(k for k in tgts if k <= t)]
    for T, fr, to in leaps:
        p = hist.get(T, {}).get(tg_at(T)); n += 1
        if not p: misses.append((sess, T, 'no target')); continue
        r = rule(fr, p)
        if r == to: ok += 1
        else:
            p1 = hist.get(T - 1, {}).get(tg_at(T))
            misses.append((sess, T, fr, to, r, p, (p[0]-2-fr[0], p[1]-2-fr[1]), 'k1 fits' if p1 and rule(fr, p1) == to else ''))
print(f"limit {LIM}: {ok}/{n}")
for m in misses: print('  ', m)
