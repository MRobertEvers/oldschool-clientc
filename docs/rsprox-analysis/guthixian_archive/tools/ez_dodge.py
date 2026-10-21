"""Ezekiel's room: every raider dodge (`tfa_ezekiel_roll` / `tfa_ezekiel_strafe`), with
its exact move, and every ordinary step, against the hazards (sword flights, the star,
the finale's strikes) and the phase.

    python3 ez_dodge.py <all-players txt> [--list]
"""
import sys, re, collections
path = sys.argv[1]
tick = None; cur = None; mode = None
ev = collections.defaultdict(list)
for l in open(path, errors='replace'):
    m = re.match(r'^\[(\d+)\]$', l)
    if m: tick = int(m[1]); continue
    m = re.match(r'^ {8}\[(\w+)\] (player|npc)=\(index=\d+, (?:name|id)=([^,]+?)(?: \(\d+\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?', l)
    if m:
        cur = (m[2], m[3]); mode = m[1]
        frm = (int(m[4]), int(m[5])); to = (int(m[6]), int(m[7])) if m[6] else None
        if m[2] == 'player' and mode in ('walk', 'run', 'teleport', 'crawl'):
            ev[tick].append(('move', m[3], mode, frm, to))
        if m[2] == 'npc' and m[3] == 'tfa_ezekiel_sword' and mode == 'teleport':
            ev[tick].append(('fly', frm, to))
        continue
    if re.match(r'^ {4}\[', l): cur = None
    if not cur: continue
    m = re.match(r'^ {12}\[sequence\] id=(\w+)', l)
    if m:
        if cur[0] == 'player' and 'tfa_ezekiel' in m[1]: ev[tick].append(('dodge', cur[1], m[1]))
        if cur[0] == 'npc' and cur[1] == 'tfa_ezekiel': ev[tick].append(('boss', m[1].replace('npc_ezekiel_', '')))
    m = re.match(r'^ {12}\[exactmove\] (.*)', l)
    if m and cur[0] == 'player': ev[tick].append(('xm', cur[1], m[1]))
dodges = []
for t in sorted(ev):
    for e in ev[t]:
        if e[0] == 'dodge':
            xm = [x[2] for x in ev[t] if x[0] == 'xm' and x[1] == e[1]]
            mv = [x for x in ev[t] if x[0] == 'move' and x[1] == e[1]]
            dodges.append((t, e[1], e[2], xm[0] if xm else None, mv[0] if mv else None))
fly = sorted({t for t in ev for e in ev[t] if e[0] == 'fly'})
bossev = [(t, e[1]) for t in sorted(ev) for e in ev[t] if e[0] == 'boss']
def near(t, ticks):
    d = [t - f for f in ticks if -3 <= t - f <= 3]
    return d
spans = []
for t, s in bossev:
    if s == 'walk01': spans.append([t, 10**9])
    if s == 'finale_death01' and spans: spans[-1][1] = t
specials = [t for t, s in bossev if s == 'special01']
def phase(t):
    return 'finale' if any(a <= t <= b for a, b in spans) else 'fight'
c = collections.Counter()
for (t, who, seq, xm, mv) in dodges:
    d = None
    if mv and mv[4]: d = (mv[4][0] - mv[3][0], mv[4][1] - mv[3][1])
    key = (phase(t), seq.replace('tfa_ezekiel_', ''), mv[2] if mv else None, f"{max(abs(d[0]), abs(d[1])) if d else '?'}t")
    c[key] += 1
    if '--list' in sys.argv:
        print(t, phase(t), who[:10], seq, mv[2] if mv else None, d, xm, 'fly' + str(near(t, fly)) if near(t, fly) else '', 'S+' + str(min([t - x for x in specials if t >= x] or [-1])))
for k, v in sorted(c.items()): print(v, k)
# ordinary moves vs dodges on/around the sword flight ticks (fight phase)
around = collections.Counter()
for f in fly:
    for off in (-1, 0, 1):
        for e in ev.get(f + off, []):
            if e[0] == 'move':
                dg = any(x[0] == 'dodge' and x[1] == e[1] for x in ev[f + off])
                around[(off, 'dodge' if dg else e[2])] += 1
print('moves around a flight tick (offset, kind):', dict(sorted(around.items())))
