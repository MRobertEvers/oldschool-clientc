"""Splats on raiders by cause and by the raider's overhead prayer at that tick
(appearance [status] overheadicon: 0 melee, 1 missiles, 2 magic; no status = none).
Causes: Dreadnoth's melee01 (raider within 1 of his 5x5, splat on the swing tick), a
jelly's jelly_attack (raider on a side of it, same tick), his slam (jump01 +1, all).
usage: dn_prayhits.py <--all-players --bas txt>..."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
HIT = re.compile(r'^ {12}\[hit\] id=(\d+), value=(-?\d+)')
STATUS = re.compile(r'^ {16}\[status\] overheadicon=(\d+)')
APP = re.compile(r'^ {12}\[appearance\]')
NAMES = {0: 'melee', 1: 'missiles', 2: 'magic', -1: 'none'}
def boxd(sw, p, n):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + n - 1) if v > lo + n - 1 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
tab = collections.defaultdict(list)
for path in sys.argv[1:]:
    tick = 0; pos = {}; start = {}; who = None; npc = None; pray = {}; inapp = None
    hits = collections.defaultdict(list); prayat = {}; events = []
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1)); start = dict(pos); who = None; npc = None; inapp = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); who = idx; npc = None; inapp = None
            c = (int(m.group(4)), int(m.group(5))); start.setdefault(idx, c)
            pos[idx] = (int(m.group(7)), int(m.group(8))) if m.group(7) else c; continue
        m = NPC.match(line)
        if m:
            who = None; inapp = None
            npc = (m.group(3), (int(m.group(8)), int(m.group(9))) if m.group(8) else (int(m.group(5)), int(m.group(6))))
            continue
        if who is not None:
            if APP.match(line): inapp = who; pray[who] = -1; continue
            m = STATUS.match(line)
            if m and inapp == who: pray[who] = int(m.group(1)); continue
            m = HIT.match(line)
            if m: hits[(tick, who)].append(int(m.group(2))); prayat[(tick, who)] = pray.get(who, -2)
            continue
        if npc:
            m = SEQ.match(line)
            if m: events.append((tick, npc[0], npc[1], m.group(1), dict(start)))
    for t, typ, at, seq, st in events:
        if typ == 'tfa_jelly_boss' and seq == 'dreadnoth_melee01':
            for i, p in st.items():
                if boxd(at, p, 5) <= 1:
                    for v in hits.get((t, i), []): tab[('melee', prayat[(t, i)])].append(v)
        if typ == 'tfa_jelly_boss' and seq == 'dreadnoth_jump01':
            for i in st:
                for v in hits.get((t + 1, i), []): tab[('slam J+1', prayat[(t + 1, i)])].append(v)
        if typ == 'tfa_jelly_minion' and seq == 'jelly_attack':
            for i, p in st.items():
                if abs(p[0] - at[0]) + abs(p[1] - at[1]) == 1:
                    for v in hits.get((t, i), []): tab[('jelly', prayat[(t, i)])].append(v)
for (cause, pr), vs in sorted(tab.items()):
    vs.sort(); z = sum(1 for v in vs if v == 0)
    print(f"{cause:9s} {NAMES.get(pr, pr):9s} n={len(vs):4d} zero={z:4d} max={vs[-1]:3d}  nonzero {[v for v in vs if v][:60]}")
