#!/usr/bin/env python3
"""Jal-MejRah (npc 7692) analysis of observed_npc_events.tsv -> BAT_ANALYSIS.txt (one finding per line).
Run: python3 bat_analysis.py > BAT_ANALYSIS.txt   (written 2026-10-04 by the Inferno bat spec worker)
Only OBSERVED events: NPC_SPAWN / NPC_ATTACK(anim 7578, attack id 70 bat_auto) / NPC_DEATH. Tick 0 = the tick of the
`Wave: N` chat line (blert/PROVENANCE.md). Local tile = world - (2240, 5312)."""
import csv, collections, os
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'observed_npc_events.tsv')
rows = [r for r in csv.DictReader(open(P), delimiter='\t') if r['npc_id'] == '7692']
by = collections.defaultdict(list)
for r in rows:
    by[(r['run'], r['wave'], r['room_id'])].append(r)
runs = len(set(r['run'] for r in rows))
spawn0 = collections.Counter(); late = []; first = []; gaps = collections.Counter(); attack_ids = collections.Counter()
perwave = collections.Counter(); noattack = 0; life = []
for k, v in by.items():
    s = [r for r in v if r['kind'] == 'spawn'][0]
    a = sorted(int(r['tick']) for r in v if r['kind'] == 'attack')
    d = [int(r['tick']) for r in v if r['kind'] == 'death']
    for r in v:
        if r['kind'] == 'attack': attack_ids[r['attack_id'] + '/' + r['attack']] += 1
    if int(s['tick']) == 0:
        spawn0[(int(s['x']) - 2240, int(s['y']) - 5312)] += 1
        perwave[(k[0], k[1])] += 1
        if a: first.append(a[0])
        if d: life.append(d[0])
    else:
        late.append(int(k[1]))
    if not a: noattack += 1
    for x, y in zip(a, a[1:]): gaps[y - x] += 1
first.sort(); n = len(first)
print('runs %d; bats observed %d (spawn rows 7692); spawned on tick 0: %d; spawned later: %d (waves %d-%d, a revive)' % (runs, len(by), sum(spawn0.values()), len(late), min(late), max(late)))
print('attack animation ids on npc 7692: %s' % dict(attack_ids))
print('bats that never attacked before dying: %d of %d' % (noattack, len(by)))
tot = sum(gaps.values())
print('gaps between one bat\'s attack ticks: total %d; gap 3: %d; gap <3: %d; gap 4-9: %d; gap >=10: %d' % (tot, gaps[3], sum(c for g, c in gaps.items() if g < 3), sum(c for g, c in gaps.items() if 4 <= g <= 9), sum(c for g, c in gaps.items() if g >= 10)))
print('gap histogram (gap:count) %s' % ' '.join('%d:%d' % kv for kv in sorted(gaps.items())[:12]))
print('first attack tick (tick-0 spawns that attacked, from the wave message): n %d min %d p10 %d median %d p90 %d max %d' % (n, first[0], first[n // 10], first[n // 2], first[9 * n // 10], first[-1]))
print('tick-0 spawn tiles, local x,z:count %s' % ' '.join('%d,%d:%d' % (x, z, c) for (x, z), c in sorted(spawn0.items())))
print('tiles used by tick-0 spawns: %d distinct' % len(spawn0))
print('bats per wave (tick-0, per run): %s' % dict(sorted(collections.Counter(perwave.values()).items())))
pw = collections.defaultdict(set)
for (run, w), c in perwave.items(): pw[int(w)].add(c)
print('bat count by wave number: %s' % ' '.join('w%d:%s' % (w, '/'.join(map(str, sorted(c)))) for w, c in sorted(pw.items())))
lc = collections.Counter(late)
print('late (revived) bat spawns by wave: %s' % ' '.join('w%d:%d' % kv for kv in sorted(lc.items())))
life.sort(); print('death tick of tick-0 bats: n %d median %d' % (len(life), life[len(life) // 2]))
