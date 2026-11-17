#!/usr/bin/env python3
"""Statistics over every Zulrah fight: snakelings, clouds, venom, damage."""
import sys
import os
from collections import Counter, defaultdict
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from zfight import fights, BOSS

R = os.path.expanduser('~/.rsprox-index-cache/home-raw/.rsprox/binary/archive/')
paths = [R + 'idx3259.txt', R + 'idx819.txt', R + 'idx3324.txt']

sn_gap = Counter()
sn_first = Counter()
sn_life = Counter()
sn_kind = Counter()
sn_angle = defaultdict(Counter)
sn_steps = Counter()
cloud_rot = Counter()
cloud_life = Counter()
venom_vals = []
venom_gaps = Counter()
cloud_hits = []
zul = defaultdict(list)     # (style, prayed) -> values
sn_dmg = defaultdict(list)  # (kind, prayed) -> values
sn_melee_delay = Counter()
egg_land = Counter()
proj_fields = defaultdict(Counter)
cloud_order = Counter()

for path in paths:
    for f in fights(path):
        me = f['me']
        idx = f['idx']
        pray = {'magic': 0, 'missiles': 0, 'melee': 0}
        mins = {}      # idx -> dict(add, name, last_attack, attacks, killed)
        clouds = {}
        ptile = None
        pend = defaultdict(list)
        last_venom = None
        egg_throws = []
        for t, ev in f['ticks']:
            start = ptile
            for e in ev:
                if e['k'] == 'varbit' and e['name'].startswith('prayer_protectfrom'):
                    pray[e['name'][len('prayer_protectfrom'):]] = e['new']
            for e in ev:
                if e['k'] == 'pl' and e['idx'] == me:
                    start = (e['x'], e['z'])
                    ptile = (e.get('nx', e['x']), e.get('nz', e['z']))
            for e in ev:
                if e['k'] == 'npc' and e['name'].startswith('snakeboss_minion'):
                    if e['mv'] == 'add':
                        mins[e['idx']] = dict(add=t, name=e['name'], last=None, n=0, tile=(e['x'], e['z']), killed=None)
                        sn_kind[e['name']] += 1
                        sn_angle[(e['x'], e['z'])][e.get('spawnangle')] += 1
                    elif e['mv'] == 'del' and e['idx'] in mins:
                        m = mins[e['idx']]
                        sn_life[(t - m['add'], 'killed' if m['killed'] else 'expired')] += 1
                    elif e['mv'] in ('walk', 'run'):
                        sn_steps[e['mv']] += 1
                if e['k'] == 'npcx' and e['idx'] in mins:
                    m = mins[e['idx']]
                    if e['blk'] == 'sequence' and 'pet_attack' in e['body']:
                        if m['last'] is None:
                            sn_first[(m['name'][17:], t - m['add'])] += 1
                        else:
                            sn_gap[(m['name'][17:], t - m['last'])] += 1
                        m['last'] = t
                    if e['blk'] == 'hit' and m['killed'] is None:
                        m['killed'] = t
                if e['k'] == 'locadd' and e['name'] == 'snakeboss_poisoncloud':
                    cloud_rot[e['rot']] += 1
                    clouds[(e['x'], e['z'])] = t
                if e['k'] == 'locdel' and (e['x'], e['z']) in clouds:
                    cloud_life[t - clouds.pop((e['x'], e['z']))] += 1
                if e['k'] == 'proj' and e['name'].startswith('snakeboss_'):
                    key = (e['name'], e['st'], e['et'], e['ang'], e['prog'], e['sh'], e['eh'], e['pkt'])
                    proj_fields[e['name']][key] += 1
            # player splats
            hits = []
            for e in ev:
                if e['k'] == 'plx' and e['idx'] == me and e['blk'] == 'hit':
                    b = dict(x.split('=') for x in e['body'].split(', '))
                    hits.append((int(b['id']), int(b['value'])))
            for hid, v in hits:
                if hid == 5:
                    if v >= 6 and v % 2 == 0:
                        venom_vals.append(v)
                        if last_venom is not None:
                            venom_gaps[t - last_venom] += 1
                        last_venom = t
            # clouds
            def covering(tile):
                return [a for a in clouds if tile and a[0] <= tile[0] <= a[0] + 2 and a[1] <= tile[1] <= a[1] + 2]
            cs, ce = covering(start), covering(ptile)
            small5 = [v for hid, v in hits if hid == 5 and v <= 5]
            if cs or ce or small5:
                cloud_hits.append((t, len(cs), len(ce), len(set(cs) | set(ce)), small5))
            # Zulrah attack landings and snakeling spells
            pr = dict(pray)
            for e in ev:
                if e['k'] == 'proj' and e.get('tkind') == 'player' and e.get('tidx') == me:
                    land = t + (e['et'] + 15) // 30
                    if e['name'] == 'snakeboss_orb':
                        pend[land].append(('Z', 'rng', pr['missiles']))
                    elif e['name'] == 'snakeboss_fireball':
                        pend[land].append(('Z', 'mag', pr['magic']))
                    elif e['name'] == 'snakeboss_minion_spell':
                        pend[land].append(('S', 'mag', pr['magic']))
            mel = 0
            for e in ev:
                if e['k'] == 'npcx' and e['idx'] in mins and e['blk'] == 'sequence' and 'pet_attack' in e['body'] \
                        and mins[e['idx']]['name'] == 'snakeboss_minion_melee':
                    mel += 1
            due = pend.pop(t, [])
            if mel:
                due = due + [('M', 'stab', pray['melee'])] * mel
            nonvenom = [(hid, v) for hid, v in hits if hid in (12, 16, 43)]
            if due and len(nonvenom) == len(due) and len(set((d[0], d[1]) for d in due)) == 1:
                for (hid, v), d in zip(nonvenom, due):
                    if d[0] == 'Z':
                        zul[(d[1], d[2])].append(v)
                    else:
                        sn_dmg[(d[0] + d[1], d[2])].append(v)
            elif due and nonvenom:
                # ambiguous mixes: keep only the case of one Zulrah attack + n zeros/snakelings where all
                # snakeling hits are zero and exactly one nonzero could be Zulrah's? skip.
                pass

print('snakeling kinds', dict(sn_kind))
print('snakeling first attack offset', sorted(sn_first.items()))
print('snakeling attack gaps', sorted(sn_gap.items()))
print('snakeling lifetimes', sorted(sn_life.items()))
print('snakeling steps', dict(sn_steps))
print('cloud rotations', dict(cloud_rot))
print('cloud lifetimes', dict(cloud_life))
print('venom values', Counter(venom_vals))
print('venom gaps', sorted(venom_gaps.items())[:20])
for k in sorted(zul):
    vs = zul[k]
    print('zulrah', k, 'n', len(vs), 'zeros', sum(1 for v in vs if v == 0), 'max', max(vs), sorted(vs))
for k in sorted(sn_dmg):
    vs = sn_dmg[k]
    print('snakeling', k, 'n', len(vs), 'zeros', sum(1 for v in vs if v == 0), 'max', max(vs), sorted(vs))
print('projectiles:')
for n, c in proj_fields.items():
    for k, v in c.most_common(6):
        print('  ', v, k)
cv = Counter()
for t, cs, ce, cu, small in cloud_hits:
    cv[(cs, ce, cu, len(small))] += 1
print('cloud splats (start clouds, end clouds, union, n small venom splats):', sorted(cv.items()))
print('cloud small values', Counter(v for t, cs, ce, cu, s in cloud_hits for v in s))
print('spawn angle per tile', {k: dict(v) for k, v in sn_angle.items()})
print('two-splat cloud ticks:', [(t, s) for t, cs, ce, cu, s in cloud_hits if len(s) == 2][:25])
