#!/usr/bin/env python3
"""Jal-ImKot (npc 7697 'meleer') analysis of observed_npc_events.tsv -> MELEE_ANALYSIS.txt (one finding per line).
Run: python3 melee_analysis.py > MELEE_ANALYSIS.txt   (written 2026-10-04 by the Inferno melee spec worker)
Only OBSERVED events: NPC_SPAWN / NPC_ATTACK (anim 7597 id 77 meleer_auto, anim 7600 id 78 meleer_dig) / NPC_DEATH.
Tick 0 = the tick of the `Wave: N` chat line (blert/PROVENANCE.md). Local tile = world - (2240, 5312)."""
import csv, collections, os
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'observed_npc_events.tsv')
rows = [r for r in csv.DictReader(open(P), delimiter='\t') if r['npc_id'] == '7697']
by = collections.defaultdict(list)
for r in rows: by[(r['run'], r['wave'], r['room_id'])].append(r)
runs = len(set(r['run'] for r in rows))
spawn0 = collections.Counter(); late = []; ids = collections.Counter(); perwave = collections.Counter()
auto_gap = collections.Counter(); dig_gap = collections.Counter(); first_auto = []; first_dig = []
after_dig = collections.Counter(); life = []; noact = 0; split_gap = collections.Counter()
for k, v in by.items():
    s = [r for r in v if r['kind'] == 'spawn'][0]
    ev = sorted((int(r['tick']), r['attack_id']) for r in v if r['kind'] == 'attack')
    for r in v:
        if r['kind'] == 'attack': ids[r['attack_id'] + '/' + r['attack']] += 1
    d = [int(r['tick']) for r in v if r['kind'] == 'death']
    autos = [t for t, a in ev if a == '77']; digs = [t for t, a in ev if a == '78']
    if int(s['tick']) == 0:
        spawn0[(int(s['x']) - 2240, int(s['y']) - 5312)] += 1; perwave[(k[0], k[1])] += 1
        if autos: first_auto.append(autos[0])
        if digs: first_dig.append(digs[0])
        if d: life.append(d[0])
    else: late.append(int(k[1]))
    if not ev: noact += 1
    for x, y in zip(autos, autos[1:]): auto_gap[y - x] += 1
    for x, y in zip(digs, digs[1:]): dig_gap[y - x] += 1
    # first auto after each dig
    for dt in digs:
        nx = [t for t in autos if t > dt]
        if nx: after_dig[nx[0] - dt] += 1
def pct(a, f):
    a = sorted(a); return a[min(len(a) - 1, int(len(a) * f))] if a else -1
print('runs %d; melee observed %d (spawn rows 7697); spawned on tick 0: %d; spawned later: %d (waves %s)' % (runs, len(by), sum(spawn0.values()), len(late), sorted(set(late))[:12]))
print('attack animation ids on npc 7697: %s' % dict(ids))
print('melee that never acted before dying: %d of %d' % (noact, len(by)))
t = sum(auto_gap.values())
print('gaps between one melee\'s auto-attack ticks: total %d; gap 4: %d; gap <4: %d; gap 5-9: %d; gap >=10: %d' % (t, auto_gap[4], sum(c for g, c in auto_gap.items() if g < 4), sum(c for g, c in auto_gap.items() if 5 <= g <= 9), sum(c for g, c in auto_gap.items() if g >= 10)))
print('auto gap histogram (gap:count) %s' % ' '.join('%d:%d' % kv for kv in sorted(auto_gap.items())[:12]))
print('dig-to-dig gaps: total %d min %s histogram(first 14) %s' % (sum(dig_gap.values()), min(dig_gap) if dig_gap else None, ' '.join('%d:%d' % kv for kv in sorted(dig_gap.items())[:14])))
print('ticks from a dig animation to that melee\'s next auto attack: n %d histogram %s' % (sum(after_dig.values()), ' '.join('%d:%d' % kv for kv in sorted(after_dig.items())[:16])))
for nm, a in (('first auto attack tick', first_auto), ('first dig tick', first_dig), ('death tick', life)):
    print('%s (tick-0 spawns): n %d min %d p10 %d median %d p90 %d max %d' % (nm, len(a), min(a), pct(a, .1), pct(a, .5), pct(a, .9), max(a)))
print('tick-0 spawn tiles, local x,z:count %s' % ' '.join('%d,%d:%d' % (x, z, c) for (x, z), c in sorted(spawn0.items())))
print('tiles used by tick-0 spawns: %d distinct' % len(spawn0))
print('melee per wave (tick-0, per run): %s' % dict(sorted(collections.Counter(perwave.values()).items())))
pw = collections.defaultdict(set)
for (run, w), c in perwave.items(): pw[int(w)].add(c)
print('melee count by wave number: %s' % ' '.join('w%d:%s' % (w, '/'.join(map(str, sorted(c)))) for w, c in sorted(pw.items())))
lc = collections.Counter(late); print('late (revived) melee spawns by wave: %s' % ' '.join('w%d:%d' % kv for kv in sorted(lc.items())))
