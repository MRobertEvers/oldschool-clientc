"""Jellies standing on their target's tile at the start of a tick: the step they take
that tick (direction, or none), counted; and whether a cardinal step is blocked."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
out = collections.Counter(); seq_then = collections.Counter(); nxt = collections.Counter()
for path in sys.argv[1:]:
    tick = 0; pos = {}; start = {}; cur = None; tgt = {}; ev = []; pend = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            for j, c, n, s in ev:
                t = tgt.get(j)
                if t is None or t not in start: continue
                if start[t] == c:
                    d = (n[0] - c[0], n[1] - c[1])
                    out[d] += 1
                    if s: seq_then[s] += 1
            tick = int(m.group(1)); start = dict(pos); ev = []; cur = None; continue
        m = PL.match(line)
        if m:
            idx = int(m.group(2)); c = (int(m.group(4)), int(m.group(5)))
            n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            start.setdefault(idx, c); pos[idx] = n; cur = None; continue
        m = NPC.match(line)
        if m:
            cur = None
            if m.group(3) != 'tfa_jelly_minion' or m.group(1) in ('add', 'del'): continue
            c = (int(m.group(5)), int(m.group(6))); n = (int(m.group(8)), int(m.group(9))) if m.group(8) else c
            cur = len(ev); ev.append([int(m.group(2)), c, n, None]); continue
        if cur is None: continue
        m = FACEP.match(line)
        if m: tgt[ev[cur][0]] = int(m.group(1)); continue
        m = SEQ.match(line)
        if m: ev[cur][3] = m.group(1)
names = {(0,1):'N',(0,-1):'S',(1,0):'E',(-1,0):'W',(0,0):'stay'}
print({names.get(k, k): v for k, v in sorted(out.items(), key=lambda kv: -kv[1])}, dict(seq_then))
