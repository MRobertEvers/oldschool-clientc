#!/usr/bin/env python3
"""Verzik P3 autos (Blert Normal+Hard, recorder as target): the hit lands at T+3. Group the single-drop
autos by the recorder's matching prayer at the THROW tick T and at the LANDING tick T+3; if the prayer
counts at the landing, the (OFF at T, ON at T+3) group looks like (ON, ON), not like (OFF, OFF)."""
import glob, json, collections, statistics
D = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_verzik/'
MAG, MIS = 1 << 16, 1 << 17
CODES = {20: ('RANGE', MIS), 21: ('MAGE', MAG)}
offs = collections.Counter(); groups = collections.defaultdict(list)
for f in sorted(glob.glob(D + '1[12]_*.json')):
    ev = json.load(open(f)); mode = 'N' if f.split('/')[-1].startswith('11') else 'H'
    prim = None; pr = {}; hp = {}
    for e in ev:
        if e['type'] == 4 and e['player'].get('source') == 0:
            prim = e['player']['name']; t = e['tick']; pr[t] = e['player'].get('prayerSet', 0)
            h = e['player'].get('hitpoints'); hp[t] = (h >> 16) if isinstance(h, int) else None
    for e in ev:
        if e['type'] != 10 or e['npcAttack']['attack'] not in CODES or e['npcAttack'].get('target') != prim: continue
        name, bit = CODES[e['npcAttack']['attack']]; T = e['tick']
        drops = [(o, hp[T + o] - hp[T + o + 1]) for o in range(0, 7)
                 if hp.get(T + o) is not None and hp.get(T + o + 1) is not None and hp[T + o + 1] < hp[T + o]]
        for o, _ in drops: offs[o] += 1
        if len(drops) != 1 or drops[0][0] != 3: continue   # one drop, at the landing tick only
        at_T = bool(pr.get(T, 0) & bit); at_L = bool(pr.get(T + 3, 0) & bit)
        groups[(mode, name, at_T, at_L)].append(drops[0][1])
print('drop offsets after the throw (all autos):', dict(sorted(offs.items())))
print('mode style prayer@throw prayer@land  n  max  mean  >16')
for k in sorted(groups):
    v = groups[k]
    print('%s   %-5s %-5s %-5s %3d %4d %5.1f %3d' % (k[0], k[1], 'ON' if k[2] else 'off', 'ON' if k[3] else 'off', len(v), max(v), statistics.mean(v), sum(1 for x in v if x > 16)))
