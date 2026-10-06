#!/usr/bin/env python3
"""Jal-Xil (npc 7698 'ranger') analysis of observed_npc_events.tsv -> RANGER_ANALYSIS.txt (one finding per line).
Run: python3 ranger_analysis.py > RANGER_ANALYSIS.txt   (written 2026-10-04 by the Inferno ranger spec worker)
Only OBSERVED events: NPC_SPAWN / NPC_ATTACK (anim 7605 id 79 ranger_auto, anim 7604 id 80 ranger_melee) / NPC_DEATH.
Tick 0 = the tick of the `Wave: N` chat line (blert/PROVENANCE.md). Local tile = world - (2240, 5312)."""
import csv, collections, os
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'observed_npc_events.tsv')
allrows = list(csv.DictReader(open(P), delimiter='\t'))
rows = [r for r in allrows if r['npc_id'] == '7698']
by = collections.defaultdict(list)
for r in rows: by[(r['run'], r['wave'], r['room_id'])].append(r)
def pct(a, p):
    a = sorted(a); return a[min(len(a) - 1, int(p * len(a)))]
runs = len(set(r['run'] for r in allrows))
ids = collections.Counter(); gap = collections.Counter(); gapany = collections.Counter(); first = []; life = []
spawn0 = collections.Counter(); late = collections.Counter(); perwave = collections.Counter(); noact = 0
meleegap = collections.Counter(); lastmelee = 0; after_melee = collections.Counter()
for k, v in by.items():
    s = [r for r in v if r['kind'] == 'spawn'][0]
    ev = sorted((int(r['tick']), r['attack_id']) for r in v if r['kind'] == 'attack')
    for t, a in ev: ids[a] += 1
    d = [int(r['tick']) for r in v if r['kind'] == 'death']
    if not ev: noact += 1
    for i in range(1, len(ev)):
        g = ev[i][0] - ev[i - 1][0]; gapany[g] += 1
        if ev[i][1] == ev[i - 1][1] == '79': gap[g] += 1
        if ev[i][1] == '80' or ev[i - 1][1] == '80': meleegap[g] += 1
    if int(s['tick']) == 0:
        spawn0[(int(s['x']) - 2240, int(s['y']) - 5312)] += 1; perwave[(k[0], k[1])] += 1
        if ev: first.append(ev[0][0])
        if d: life.append(d[0])
    else: late[int(k[1])] += 1
print('runs %d; ranger (7698) spawn rows %d; zuk-set ranger (7702) spawn rows %d' % (runs, len(by), sum(1 for r in allrows if r['npc_id'] == '7702' and r['kind'] == 'spawn')))
print('attack animation ids on npc 7698: %s' % dict(ids))
print('ranger that never acted before dying: %d of %d' % (noact, len(by)))
print('gaps between two consecutive ranged auto attacks of one ranger: total %d; gap 4: %d; gap <4: %d; gap 5-9: %d; gap >=10: %d' % (sum(gap.values()), gap[4], sum(c for g, c in gap.items() if g < 4), sum(c for g, c in gap.items() if 5 <= g <= 9), sum(c for g, c in gap.items() if g >= 10)))
print('auto gap histogram (gap:count) %s' % ' '.join('%d:%d' % kv for kv in sorted(gap.items())[:14]))
print('gap between any two consecutive attacks of one ranger: total %d; min %d; gap 4: %d; gap<4: %d' % (sum(gapany.values()), min(gapany), gapany[4], sum(c for g, c in gapany.items() if g < 4)))
print('gaps touching a melee attack (either side): total %d histogram %s' % (sum(meleegap.values()), ' '.join('%d:%d' % kv for kv in sorted(meleegap.items())[:10])))
print('melee share of all attack animations: %d of %d' % (ids['80'], ids['79'] + ids['80']))
print('first attack tick (tick-0 spawns): n %d min %d p10 %d median %d p90 %d max %d' % (len(first), min(first), pct(first, .1), pct(first, .5), pct(first, .9), max(first)))
print('death tick (tick-0 spawns): n %d min %d p10 %d median %d p90 %d max %d' % (len(life), min(life), pct(life, .1), pct(life, .5), pct(life, .9), max(life)))
print('tick-0 spawn tiles, local x,z:count %s' % ' '.join('%d,%d:%d' % (x, z, c) for (x, z), c in sorted(spawn0.items())))
print('tiles used by tick-0 spawns: %d distinct' % len(spawn0))
pc = collections.Counter(perwave.values()); print('ranger per wave (tick-0, per run): %s' % dict(sorted(pc.items())))
pw = collections.defaultdict(set)
for (run, w), c in perwave.items(): pw[int(w)].add(c)
print('ranger count by wave number: %s' % ' '.join('w%d:%s' % (w, '/'.join(map(str, sorted(c)))) for w, c in sorted(pw.items())))
print('late (revived or later) ranger spawn rows by wave: %s' % (' '.join('w%d:%d' % kv for kv in sorted(late.items())) or 'none'))
