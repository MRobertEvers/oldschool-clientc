"""Ezekiel phase 1: on each sword flight tick, every raider who moved -- dodge or
ordinary step -- with whether their start tile was inside a flying sword's 3-wide band,
whether their end tile was, and their splats that tick.

    python3 ez_dodge_band.py <all-players txt>...
"""
import sys, re, collections
tot = collections.Counter()
for path in sys.argv[1:]:
    tick = None; cur = None
    ev = collections.defaultdict(list)
    for l in open(path, errors='replace'):
        m = re.match(r'^\[(\d+)\]$', l)
        if m: tick = int(m[1]); continue
        m = re.match(r'^ {8}\[(\w+)\] (player|npc)=\(index=\d+, (?:name|id)=([^,]+?)(?: \(\d+\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?', l)
        if m:
            cur = (m[2], m[3], m[1])
            frm = (int(m[4]), int(m[5])); to = (int(m[6]), int(m[7])) if m[6] else frm
            if m[2] == 'player': ev[tick].append(['pl', m[3], m[1], frm, to, None, []])
            if m[2] == 'npc' and m[3] == 'tfa_ezekiel_sword' and m[1] == 'teleport': ev[tick].append(['fly', frm, to])
            continue
        if re.match(r'^ {4}\[', l): cur = None
        if not cur or cur[0] != 'player': continue
        last = [e for e in ev[tick] if e[0] == 'pl' and e[1] == cur[1]][-1]
        m = re.match(r'^ {12}\[sequence\] id=(tfa_ezekiel_\w+)', l)
        if m: last[5] = m[1].replace('tfa_ezekiel_', '')
        m = re.match(r'^ {12}\[hit\] id=(\d+), value=(\d+)', l)
        if m: last[6].append(int(m[2]))
    for t in sorted(ev):
        flies = [e for e in ev[t] if e[0] == 'fly']
        if not flies: continue
        def inband(p):
            for _, f, to in flies:
                if to[0] == f[0]:
                    if f[0] <= p[0] <= f[0] + 2 and 5528 <= p[1] <= 5540: return True
                elif f[1] <= p[1] <= f[1] + 2 and 4056 <= p[0] <= 4068: return True
            return False
        for e in ev[t]:
            if e[0] != 'pl' or e[4] == e[3] or not (4056 <= e[3][0] <= 4068 and 5528 <= e[3][1] <= 5540): continue
            kind = e[5] or e[2]
            a, b = inband(e[3]), inband(e[4])
            tot[(kind, 'start in' if a else 'start out', 'end in' if b else 'end out')] += 1
            if len(sys.argv) > 2 and '--list' in sys.argv: print(path[-8:-4], t, e[1][:9], kind, e[3], '->', e[4], a, b, e[6])
for k, v in sorted(tot.items()): print(v, k)
