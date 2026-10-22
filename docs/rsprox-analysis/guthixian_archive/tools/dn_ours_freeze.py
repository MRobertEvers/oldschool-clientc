"""Our tick log: each cast/spec the raider makes at Dreadnoth (player_anim rows), the
boss's next step after it, and the scorching arrow's landing (its impact spotanim on him
+ delay). usage: dn_ours_freeze.py <ticklog.tsv>"""
import sys, collections
rows = collections.defaultdict(list)
for line in open(sys.argv[1], errors='replace'):
    f = line.rstrip('\n').split('\t')
    if len(f) < 9 or not f[1].isdigit(): continue
    rows[int(f[1])].append(f[2:])
steps = [t for t in sorted(rows) for r in rows[t] if r[0] == 'npc_tile' and r[5] == '16665']
marks = {t: r[7] for t in rows for r in rows[t] if r[0] == 'mark'}
casts = [(t, r[2]) for t in sorted(rows) for r in rows[t] if r[0] == 'player_anim' and r[2] not in ('-1', '0')]
for t, seq in casts:
    after = [s for s in steps if s > t]
    lands = [(tt, r) for tt in range(t, t + 6) for r in rows.get(tt, []) if r[0] in ('npc_spotanim',)]
    print(f"cast [{t}] seq {seq}: next boss step [{after[0] if after else None}] = C+{after[0] - t if after else '-'}  npc spotanims {[(tt, r[2], r[4] if len(r) > 4 else '') for tt, r in lands]}")
print('marks', sorted(marks.items()))
