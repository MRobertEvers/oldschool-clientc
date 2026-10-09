#!/usr/bin/env python3
"""THE MAIDEN'S CRAB WAVES, Blert against ours, per wave (70/50/30) and per
spawn point: spawn timing against her hitpoints, the points drawn, the
scuffed rate, each crab's path tile by tile while it is not frozen, the leak
(tick, tile, heal), and a frozen crab's hold and its walk after the thaw.

  tools/blert_maiden_crab_cal.py build/blert/maiden <our runs dir> [<our runs dir> ...]

Room-local tiles: O = her south-west tile - (26,28). A crab's path is cut at
the first ICE BARRAGE on it (Blert: the cast; ours: the impact graphic), its
death or its leak."""
import collections, glob, json, os, sys
POINTS = {(37, 40): 'N1', (41, 40): 'N2', (45, 40): 'N3', (49, 38): 'N4i', (49, 40): 'N4o',
          (37, 20): 'S1', (41, 20): 'S2', (45, 20): 'S3', (49, 22): 'S4i', (49, 20): 'S4o'}
SCUFF = {(38, 41): 'N1', (42, 41): 'N2', (46, 41): 'N3', (50, 39): 'N4i', (50, 41): 'N4o',
         (38, 19): 'S1', (42, 19): 'S2', (46, 19): 'S3', (50, 21): 'S4i', (50, 19): 'S4o'}
ORDER = ['N1', 'N2', 'N3', 'N4i', 'N4o', 'S1', 'S2', 'S3', 'S4i', 'S4o']
FORMS = {8360, 8361, 8362, 8363}
CRAB = 8366
ATK = {x['protoId']: x['name'] for x in json.load(open(
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs', 'minigames', 'theater_of_blood', 'sources', 'blert_api', 'reference', 'attack_definitions.json')))}
FREEZE = {k for k, v in ATK.items() if 'BARRAGE' in v or v.startswith('ACCURSED_SCEPTRE')}
LEAK = (24, 26, 32, 34)
THR = (0.7, 0.5, 0.3)


def point_of(tile):
    if tile in POINTS: return POINTS[tile], False
    if tile in SCUFF: return SCUFF[tile], True
    return '?%d,%d' % tile, False


def blert(d):
    crabs, waves = [], []
    for f in sorted(glob.glob(d + '/*-*.json')):
        ev = sorted(json.load(open(f)), key=lambda e: e['tick'])
        base = None
        for e in ev:
            n = e.get('npc') or {}
            if e['type'] in (7, 8) and n.get('id') in FORMS:
                base = (e['xCoord'] - 26, e['yCoord'] - 28); break
        if base is None: continue
        hp = {}                                     # tick -> (cur, max)
        cs = {}
        for e in ev:
            n = e.get('npc') or {}
            if n.get('id') in FORMS and e['type'] in (7, 8):
                hp[e['tick']] = (n['hitpoints'] >> 16, n['hitpoints'] & 0xFFFF)
            if n.get('id') != CRAB: continue
            rid, t = n['roomId'], e['tick']
            tile = (e['xCoord'] - base[0], e['yCoord'] - base[1])
            if e['type'] == 7:
                mc = n.get('maidenCrab') or {}
                pt, sc = point_of(tile)
                cs[rid] = {'room': f, 'wave': mc.get('spawn'), 'pt': pt, 'scuffed': mc.get('scuffed'), 'scuffed_tile': sc,
                           't0': t, 'pos': {t: tile}, 'hp0': n['hitpoints'] >> 16, 'end': None, 'leak': None,
                           'leak_tile': None, 'leak_hp': None, 'freeze': None, 'hit': None}
            elif rid in cs:
                c = cs[rid]
                if e['type'] == 8: c['pos'][t] = tile
                if e['type'] == 9 and c['end'] is None: c['end'] = t
                if e['type'] == 100:
                    # the event is the ARRIVAL; her heal lands a tick later (451 of
                    # 670) and ours logs the heal: compare heal ticks
                    c['leak'] = t + 1; c['leak_tile'] = tile; c['leak_hp'] = n['hitpoints'] >> 16
                    c['end'] = t if c['end'] is None else min(c['end'], t)
        def tile_at(c, t):
            ks = [k for k in c['pos'] if k <= t]
            return c['pos'][max(ks)] if ks else None
        for e in ev:
            if e['type'] != 5: continue
            rid = (e['attack'].get('target') or {}).get('roomId')
            if rid in cs:
                c = cs[rid]
                if c['hit'] is None: c['hit'] = e['tick']
                if e['attack']['type'] in FREEZE:
                    # the freeze holds from cast + 1 at every distance (1,239 of
                    # 1,241 walking crabs); the 3x3 round the target freezes
                    land = e['tick'] + 1
                    p = tile_at(c, e['tick'])
                    for o in cs.values():
                        q = tile_at(o, e['tick'])
                        if p and q and o['end'] is None or (p and q and o['end'] >= e['tick']):
                            if p and q and max(abs(p[0] - q[0]), abs(p[1] - q[1])) <= 1 and o['t0'] <= e['tick']:
                                if o['freeze'] is None or land < o['freeze']:
                                    o['freeze'] = land
                                    o['aoe'] = o is not c
        ticks = sorted(hp)
        def hp_at(t):
            k = [x for x in ticks if x <= t]
            return hp[k[-1]] if k else None
        for c in cs.values():
            c['her'] = hp_at(c['t0'] - 1)
            if c['leak'] is not None:
                a, b = hp_at(c['leak'] - 2), hp_at(c['leak'])
                c['heal'] = (b[0] - a[0]) if a and b else None
        # the tick she first stood at or below each threshold
        for w, th in enumerate(THR):
            cross = next((t for t in ticks if hp[t][0] <= th * hp[t][1]), None)
            t0s = [c['t0'] for c in cs.values() if c['wave'] == w]
            if t0s:
                waves.append({'src': 'blert', 'wave': w, 'cross': cross, 't0': min(t0s),
                              'scuffed': sorted({c['scuffed'] for c in cs.values() if c['wave'] == w}),
                              'pattern': frozenset(c['pt'] for c in cs.values() if c['wave'] == w)})
        crabs += cs.values()
    return crabs, waves


def ours(dirs):
    crabs, waves = [], []
    for d in dirs:
        for sess in sorted(glob.glob(d + '/*/')):
            try:
                rows = [l.rstrip('\n').split('\t') for l in open(sess + 'ticklog.tsv') if not l.startswith('ticklog')]
            except OSError:
                continue
            base, her_max, her = None, None, None
            live, cs, hp = {}, [], {}
            spot = collections.Counter()
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == 'npc_spawn' and int(r[4]) in FORMS and base is None:
                    v = int(r[5]); base = (((v >> 14) & 0x3FFF) - 26, (v & 0x3FFF) - 28)
            if base is None: continue
            freeze_gfx = None
            for r in rows:
                if r[2] == 'npc_spotanim' and int(r[4]) == CRAB: spot[int(r[5])] += 1
            if spot: freeze_gfx = spot.most_common(1)[0][0]
            leak_heals = collections.defaultdict(list)
            dmg = collections.defaultdict(int)
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == 'npc_spawn' and int(r[4]) == CRAB:
                    v = int(r[5]); tile = (((v >> 14) & 0x3FFF) - base[0], (v & 0x3FFF) - base[1])
                    pt, sc = point_of(tile)
                    c = {'room': sess, 'wave': None, 'pt': pt, 'scuffed': sc, 'scuffed_tile': sc, 't0': t, 'pos': {t: tile},
                         'hp0': None, 'end': None, 'leak': None, 'leak_tile': None, 'leak_hp': None, 'freeze': None,
                         'hit': None, 'slot': r[3], 'heal': None, 'her': her}
                    live[r[3]] = c; cs.append(c)
                elif k == 'npc_tile' and r[3] in live and int(r[7]) == CRAB:
                    live[r[3]]['pos'][t] = (int(r[4]) - base[0], int(r[5]) - base[1])
                elif k == 'hit_npc' and int(r[4]) == CRAB and r[3] in live:
                    c = live[r[3]]; dmg[id(c)] += int(r[5]); c.setdefault('dmg_at', collections.Counter())[t] += int(r[5])
                    if c['hit'] is None: c['hit'] = t
                elif k == 'npc_spotanim' and int(r[4]) == CRAB and r[3] in live and int(r[5]) == freeze_gfx:
                    c = live[r[3]]
                    if c['freeze'] is None: c['freeze'] = t
                    if c['hit'] is None: c['hit'] = t
                elif k in ('npc_death', 'npc_free') and int(r[4]) == CRAB and r[3] in live:
                    c = live.pop(r[3]); c['end'] = t
                    v = int(r[5]); c['end_tile'] = (((v >> 14) & 0x3FFF) - base[0], (v & 0x3FFF) - base[1])
                elif k == 'npc_heal' and int(r[4]) in FORMS and 'leak' in r[9]:
                    leak_heals[t].append(int(r[5]))
            # crabs: leak = removed with a leak heal on that tick or the next, not killed by damage
            for c in cs:
                if c['end'] is None: continue
                hs = leak_heals.get(c['end'], []) + leak_heals.get(c['end'] + 1, [])
                if hs and dmg[id(c)] - c.get('dmg_at', {}).get(c['end'], 0) < 75:
                    c['leak'] = c['end']; c['leak_tile'] = c.get('end_tile'); c['heal'] = sum(hs)
            # waves: the crabs' spawn ticks in order, six a wave
            t0s = sorted({c['t0'] for c in cs})
            for c in cs: c['wave'] = t0s.index(c['t0']) if c['t0'] in t0s else None
            for w, t0 in enumerate(t0s[:3]):
                waves.append({'src': 'ours', 'wave': w, 'cross': None, 't0': t0,
                              'scuffed': sorted({c['scuffed'] for c in cs if c['t0'] == t0}),
                              'pattern': frozenset(c['pt'] for c in cs if c['t0'] == t0)})
            crabs += cs
    return crabs, waves


def path(c):
    # the freeze tick itself is already still: cut before it
    stop = [x for x in (c['freeze'] - 1 if c['freeze'] is not None else None, c['end'], c['leak']) if x is not None]
    stop = min(stop) if stop else c['t0'] + 60
    p, cur = [], None
    for t in range(c['t0'], stop + 1):
        cur = c['pos'].get(t, cur); p.append(cur)
    return p


def modal(paths, n):
    m = []
    for k in range(n):
        cnt = collections.Counter(p[k] for p in paths if len(p) > k)
        if not cnt: break
        tile, x = cnt.most_common(1)[0]
        m.append((tile, x, sum(cnt.values())))
    return m


def hist(xs):
    return ' '.join('%s:%d' % kv for kv in sorted(collections.Counter(xs).items()))


B, BW = blert(sys.argv[1])
O, OW = ours(sys.argv[2:])
print('crabs blert %d ours %d; waves blert %d ours %d' % (len(B), len(O), len(BW), len(OW)))
print('\n== SPAWN')
print(' blert spawn - threshold cross, per wave:', [hist(w['t0'] - w['cross'] for w in BW if w['wave'] == i and w['cross'] is not None) for i in range(3)])
print(' blert her hp%% at spawn-1, per wave:', [hist(round(100 * c['her'][0] / c['her'][1]) for c in B if c['wave'] == i and c['her'] and c['t0'] == min(x['t0'] for x in B if x['room'] == c['room'] and x['wave'] == i)) for i in range(3)])
for name, W in (('blert', BW), ('ours', OW)):
    print(' %s crabs/wave %s scuffed waves %s mixed %d' % (name, hist(len(w['pattern']) for w in W),
          sum(1 for w in W if w['scuffed'] == [True]), sum(1 for w in W if len(w['scuffed']) > 1)))
    pc = collections.Counter(p for w in W for p in w['pattern'])
    print('   point frequency', ' '.join('%s %.2f' % (p, pc[p] / max(1, len(W))) for p in ORDER))
print('\n== UNFROZEN PATHS per point (scuffed spawns apart); modal tile per tick, agreement')
for sc in (False, True):
    for p in ORDER:
        bp = [path(c) for c in B if c['pt'] == p and bool(c['scuffed_tile']) == sc]
        op = [path(c) for c in O if c['pt'] == p and bool(c['scuffed_tile']) == sc]
        if not bp or not op: continue
        mb, mo = modal(bp, 24), modal(op, 24)
        diffs = ['+%d b%s(%d/%d) o%s(%d/%d)' % (k, mb[k][0], mb[k][1], mb[k][2], mo[k][0], mo[k][1], mo[k][2])
                 for k in range(min(len(mb), len(mo))) if mb[k][0] != mo[k][0] and mb[k][2] >= 5]
        agree = [mb[k][1] / mb[k][2] for k in range(len(mb)) if mb[k][2] >= 5]
        print('%s%-4s n=%d/%d blert %s' % ('s' if sc else ' ', p, len(bp), len(op), ' '.join('%d,%d' % m[0] for m in mb[:20])))
        print('           ours  %s' % ' '.join('%d,%d' % m[0] for m in mo[:20]))
        print('           blert own agreement min %.2f; DIFF %s' % (min(agree) if agree else 0, ' | '.join(diffs[:5]) or '-'))
print('\n== UNTOUCHED LEAKS: leak tick - spawn, per point')
for p in ORDER:
    bl = [c['leak'] - c['t0'] for c in B if c['pt'] == p and c['leak'] is not None and c['hit'] is None]
    ol = [c['leak'] - c['t0'] for c in O if c['pt'] == p and c['leak'] is not None and c['freeze'] is None]
    bt = collections.Counter(c['leak_tile'] for c in B if c['pt'] == p and c['leak'] is not None and c['hit'] is None).most_common(2)
    ot = collections.Counter(c['leak_tile'] for c in O if c['pt'] == p and c['leak'] is not None and c['freeze'] is None).most_common(2)
    print(' %-4s blert %-22s ours %-22s | tile blert %s ours %s' % (p, hist(bl), hist(ol), bt, ot))
print('\n== HEAL per leak: heal / crab hp at leak (blert), heal (ours)')
print(' blert', hist(round(c['heal'] / c['leak_hp'], 1) for c in B if c['leak'] is not None and c.get('heal') and c['leak_hp']))
print(' blert crab hp at leak', hist(c['leak_hp'] for c in B if c['leak'] is not None and c['hit'] is None))
print(' blert heal (untouched)', hist(c['heal'] for c in B if c['leak'] is not None and c['hit'] is None and c.get('heal') is not None))
print(' ours heal', hist(c['heal'] for c in O if c['leak'] is not None))
print('\n== FROZEN: still ticks (first still tick -> next step), and from the thaw step to the leak heal, by frozen tile')
walks = {}
for name, C in (('blert', B), ('ours', O)):
    hold, walk = [], collections.defaultdict(list)
    for c in C:
        if c['freeze'] is None: continue
        P = c['pos']; ts = sorted(P)
        def at(t):
            k = [x for x in ts if x <= t]
            return P[k[-1]] if k else None
        s0 = next((t for t in range(c['freeze'] - 3, c['freeze'] + 6) if at(t) == at(t - 1) and t > c['t0']), None)
        if s0 is None: continue
        r = next((t for t in ts if t > s0 and P[t] != at(t - 1)), None)
        if r is None: continue
        hold.append(r - s0)
        if c['leak'] is not None and (c['hit'] is None or True): walk[at(s0)].append(c['leak'] - r)
    walks[name] = walk
    print(' %s still %s' % (name, hist(hold)))
for tile in sorted(set(walks['blert']) & set(walks['ours'])):
    print('   frozen at %s: blert thaw->heal %s | ours %s' % (tile, hist(walks['blert'][tile]), hist(walks['ours'][tile])))
