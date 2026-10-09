#!/usr/bin/env python3
"""Maiden crab waves in Blert, by ROLE: the freezer (whoever barrages a crab)
and the DPS (everyone else), per spawn point and per pattern.

  tools/blert_maiden_roles.py build/blert/maiden

  1. pattern stats: how many waves, how many distinct patterns.
  2. the freezer: its tile when the wave spawns, its cast schedule (offset,
     point), the pairwise precedence of points in its schedule.
  3. the DPS: first-hit offset per point, the pairwise precedence, a DPS's
     side split, and where it stands at the spawn.
  4. fate per point (killed / leaked, tick).
No player names are printed: freezer = F, DPS = D1/D2 by first attack."""
import collections, glob, json, os, sys
POINTS = {(37, 40): 'N1', (41, 40): 'N2', (45, 40): 'N3', (49, 38): 'N4i', (49, 40): 'N4o',
          (37, 20): 'S1', (41, 20): 'S2', (45, 20): 'S3', (49, 22): 'S4i', (49, 20): 'S4o',
          (38, 41): 'N1', (42, 41): 'N2', (46, 41): 'N3', (50, 39): 'N4i', (50, 41): 'N4o',
          (38, 19): 'S1', (42, 19): 'S2', (46, 19): 'S3', (50, 21): 'S4i', (50, 19): 'S4o'}
ORDER = ['N1', 'N2', 'N3', 'N4i', 'N4o', 'S1', 'S2', 'S3', 'S4i', 'S4o']
ATK = {x['protoId']: x['name'] for x in json.load(open(
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs', 'minigames', 'theater_of_blood', 'sources', 'blert_api', 'reference', 'attack_definitions.json')))}
FREEZE = {k for k, v in ATK.items() if 'BARRAGE' in v or v.startswith('ACCURSED_SCEPTRE')}


def room(path):
    ev = sorted(json.load(open(path)), key=lambda e: e['tick'])
    base = None
    for e in ev:
        n = e.get('npc') or {}
        if e['type'] in (7, 8) and n.get('id') == 8360:
            base = (e['xCoord'] - 26, e['yCoord'] - 28)
            break
    if base is None:
        return []
    pos = collections.defaultdict(dict)
    crabs = {}
    for e in ev:
        if e['type'] == 4:
            pos[e['player']['name']][e['tick']] = (e['xCoord'] - base[0], e['yCoord'] - base[1])
        n = e.get('npc') or {}
        if n.get('id') == 8366:
            c = crabs.setdefault(n['roomId'], {'spawn': None, 'wave': None, 'pt': None, 'death': None, 'leak': None, 'hp0': False})
            if e['type'] == 7:
                c['spawn'] = e['tick']
                c['wave'] = (n.get('maidenCrab') or {}).get('spawn')
                c['pt'] = POINTS.get((e['xCoord'] - base[0], e['yCoord'] - base[1]))
            if e['type'] == 9:
                c['death'] = e['tick']; c['hp0'] = (n['hitpoints'] >> 16) == 0
            if e['type'] == 100:
                c['leak'] = e['tick']
    atk = [e for e in ev if e['type'] == 5]
    freezers = {e['player']['name'] for e in atk if e['attack']['type'] in FREEZE
                and (e['attack'].get('target') or {}).get('roomId') in crabs}
    out = []
    for w in (0, 1, 2):
        cs = {rid: c for rid, c in crabs.items() if c['wave'] == w and c['spawn'] is not None and c['pt']}
        if not cs:
            continue
        t0 = min(c['spawn'] for c in cs.values())
        pattern = frozenset(c['pt'] for c in cs.values())
        casts, hits = [], collections.defaultdict(list)
        for e in atk:
            rid = (e['attack'].get('target') or {}).get('roomId')
            if rid not in cs or e['tick'] < t0:
                continue
            who = e['player']['name']
            if e['attack']['type'] in FREEZE:
                casts.append((e['tick'] - t0, cs[rid]['pt'], who))
            else:
                hits[who].append((e['tick'] - t0, cs[rid]['pt'], ATK.get(e['attack']['type'], '?')))
        def at(who, t):
            p = pos.get(who, {})
            ks = [k for k in p if k <= t]
            return p[max(ks)] if ks else None
        fate = {}
        for c in cs.values():
            fate[c['pt']] = ('L', c['leak'] - t0) if c['leak'] is not None else (('k', c['death'] - t0) if c['hp0'] else ('-', None))
        out.append({'wave': w, 't0': t0, 'pattern': pattern, 'casts': sorted(casts),
                    'freezers': sorted(freezers), 'hits': hits, 'fate': fate,
                    'f_tile': {f: at(f, t0 - 1) for f in freezers},
                    'd_tile': {d: at(d, t0 - 1) for d in hits},
                    'nplayers': len(pos)})
    return out


W = []
for f in sorted(glob.glob(sys.argv[1] + '/*-*.json')):
    W += room(f)
print('waves', len(W), 'distinct patterns', len({w['pattern'] for w in W}),
      'crabs/wave', collections.Counter(len(w['pattern']) for w in W))
print('freezers per room-wave', collections.Counter(len(w['freezers']) for w in W))
one = [w for w in W if len(w['freezers']) == 1]
print('\n== FREEZER (waves with exactly one freezer: %d)' % len(one))
for wi in (0, 1, 2):
    tiles = collections.Counter(list(w['f_tile'].values())[0] for w in one if w['wave'] == wi)
    print(' wave %d freezer tile at spawn-1 (top): %s' % (wi, tiles.most_common(6)))
# cast schedule: first cast offset, gaps, cast count
first = collections.Counter(); gaps = collections.Counter(); cnt = collections.Counter()
for w in one:
    cs = []
    for t, p, who in w['casts']:
        if not cs or t - cs[-1][0] > 1:
            cs.append((t, p))
    if cs:
        first[cs[0][0]] += 1
        for a, b in zip(cs, cs[1:]): gaps[b[0] - a[0]] += 1
    cnt[len(cs)] += 1
    w['seq'] = cs
print(' first cast offset', sorted(first.items()))
print(' cast gaps', sorted(gaps.items())[:12])
print(' casts per wave', sorted(cnt.items()))
# per point: P(frozen | present), cast index when frozen
fz = collections.Counter(); pres = collections.Counter(); idx = collections.defaultdict(collections.Counter)
for w in one:
    order = []
    for t, p in w['seq']:
        if p not in order: order.append(p)
    for p in w['pattern']:
        pres[p] += 1
        if p in order:
            fz[p] += 1; idx[p][order.index(p)] += 1
print(' point: frozen/present, freeze rank histogram')
for p in ORDER:
    print('  %-4s %3d/%3d  %s' % (p, fz[p], pres[p], sorted(idx[p].items())))
# precedence: among waves with both A and B present, freezer froze A before B
prec = collections.defaultdict(lambda: [0, 0])
for w in one:
    order = []
    for t, p in w['seq']:
        if p not in order: order.append(p)
    for a in w['pattern']:
        for b in w['pattern']:
            if a >= b: continue
            ia = order.index(a) if a in order else 99; ib = order.index(b) if b in order else 99
            if ia == ib: continue
            prec[(a, b)][0 if ia < ib else 1] += 1
print(' precedence A<B (A first, B first):')
for (a, b), (x, y) in sorted(prec.items(), key=lambda kv: (ORDER.index(kv[0][0]), ORDER.index(kv[0][1]))):
    print('   %s<%s %d:%d' % (a, b, x, y) if ORDER.index(a) < ORDER.index(b) else '   %s<%s %d:%d' % (b, a, y, x))
print('\n== DPS')
dfirst = collections.defaultdict(collections.Counter); dp = collections.defaultdict(lambda: [0, 0])
side = collections.Counter(); dtile = collections.Counter()
for w in one:
    fz_name = w['freezers'][0]
    ds = [d for d in w['hits'] if d != fz_name]
    for d in ds:
        dtile[w['d_tile'][d]] += 1
        seen = []
        for t, p, a in sorted(w['hits'][d]):
            if p not in seen:
                seen.append(p); dfirst[p][t] += 1
        sides = {p[0] for p in seen}
        side[''.join(sorted(sides))] += 1
    # team precedence: first DPS hit per point
    fh = {}
    for d in ds:
        for t, p, a in w['hits'][d]:
            fh[p] = min(fh.get(p, 999), t)
    for a in w['pattern']:
        for b in w['pattern']:
            if ORDER.index(a) >= ORDER.index(b): continue
            ta, tb = fh.get(a, 999), fh.get(b, 999)
            if ta == tb: continue
            dp[(a, b)][0 if ta < tb else 1] += 1
print(' dps first-hit offset per point:')
for p in ORDER:
    c = dfirst[p]; n = sum(c.values())
    print('  %-4s n=%3d median %s  %s' % (p, n, sorted(c.elements())[n // 2] if n else '-', sorted(c.items())[:10]))
print(' sides a DPS touched', side.most_common())
print(' dps tile at spawn-1', dtile.most_common(8))
print(' dps precedence:', ' '.join('%s<%s %d:%d' % (a, b, x, y) for (a, b), (x, y) in sorted(dp.items(), key=lambda kv: (ORDER.index(kv[0][0]), ORDER.index(kv[0][1])))))
print('\n== FATE per point')
for p in ORDER:
    c = collections.Counter(w['fate'][p][0] for w in W if p in w['fate'])
    lk = sorted(w['fate'][p][1] for w in W if p in w['fate'] and w['fate'][p][0] == 'L')
    print('  %-4s %s leak ticks %s' % (p, dict(c), lk[:12]))
