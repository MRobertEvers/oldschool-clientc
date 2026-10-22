# Ocellus phase 4 (oc_p4.py): OUR tick log (argument) as the same per-D table, hits, attack gaps.
# Run from this directory: python3 oc_p4_ours.py <ticklog.tsv>
import sys
from collections import defaultdict, Counter
f = sys.argv[1]
rows = [l.rstrip('\n').split('\t') for l in open(f) if not l.startswith('ticklog')]
E = next(int(r[1]) for r in rows if r[2] == 'npc_say' and r[9] == 'End...')
boss = next(r[3] for r in rows if r[2] == 'npc_say' and r[9] == 'End...')
D = sorted(int(r[1]) for r in rows if r[2] == 'npc_anim' and r[3] == boss and r[5] == '14863')
print('E', E, 'D', [d - E for d in D])
tab = defaultdict(Counter)
for r in rows:
    t = int(r[1])
    if t < E - 1 or not D: continue
    d = [t - x for x in D if t >= x]
    key = ('D+%d' % d[-1]) if d else ('E+%d' % (t - E))
    if r[3] == boss and r[2] in ('npc_anim', 'npc_spotanim', 'npc_face', 'npc_headicon', 'npc_info', 'npc_say', 'npc_state'):
        tab[key][' '.join(r[2:9]) + ' ' + r[9]] += 1
for k in sorted(tab, key=lambda x: (x[0] != 'E', int(x[2:]))):
    for it, n in tab[k].items(): print(k, n, it)
print('--- raider hits by D (value list)')
hits = defaultdict(list)
for r in rows:
    t = int(r[1])
    if r[2] == 'hit_player' and t >= E and D:
        d = [t - x for x in D if t >= x]
        if d: hits[d[-1]].append((r[3], r[5], r[6]))
for d in sorted(hits): print(d, hits[d])
print('--- attack gaps by D')
last = {}; gaps = defaultdict(Counter)
for r in rows:
    t = int(r[1])
    if r[2] == 'player_anim' and r[4] == '426':
        who = r[3]
        if t >= E and D and who in last:
            d = [t - x for x in D if t >= x]
            if d: gaps[d[-1]][t - last[who]] += 1
        last[who] = t
for d in sorted(gaps): print(d, dict(gaps[d]))
