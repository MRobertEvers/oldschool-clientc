#!/usr/bin/env python3
"""Every raider's move between Fractured Archive rooms, from --all-players
transcripts (nexus_plan.md §6):

    python3 doors.py ~/.rsprox-index-cache/home-all/.rsprox/binary/archive/idx43{0,1}?.txt [--summary]

Per move: the room left and the room reached (by template square and plane),
the raider's last step there (L) and the first face after it (the op tick O,
for a loc; an npc op's face comes with the click), the tick they were deleted
from the old room, the tick and tile they were added in the new one, and the
add's facing. A raider added with an `angle=` face has not moved since they
landed; one added with `type=reset` was seen late (the spectator arrived after
them) and the tile is wherever they had run to.

--summary groups by (from, to): what was faced before leaving, the fresh
landing tiles and faces, and (O - L, del - O) for the first raider of each
party through a section door.
"""
import collections
import json
import re
import sys

ROOMS = {(64, 87, 0): 'guardian', (64, 87, 2): 'atrium', (63, 86, 0): 'ezekiel', (65, 87, 0): 'bork',
         (65, 88, 0): 'dreadnoth', (65, 86, 0): 'xarth', (63, 88, 0): 'moia', (63, 87, 0): 'dagonhai',
         (64, 88, 1): 'ocellus', (64, 90, 2): 'vault', (64, 86, 0): 'lobby'}
TICK = re.compile(r'^\[(\d+)\]$')
PLAYER = re.compile(r'^        \[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)'
                    r'(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACE = re.compile(r'^            \[facing\] (.*)')


def room(c):
    return ROOMS.get((c[0] >> 6, c[1] >> 6, c[2]))


def moves(path):
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0
    cur = None
    st = {}
    out = []
    pending = {}
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        m = PLAYER.match(line)
        if m:
            kind, name = m.group(1), m.group(2)
            c = tuple(map(int, m.group(3, 4, 5)))
            nc = tuple(map(int, m.group(6, 7, 8))) if m.group(6) else c
            s = st.setdefault(name, {'c': None, 'mv': None, 'f1': None, 'del': None})
            cur = name
            if kind == 'add':
                if s['c'] and room(s['c']) and room(c) and room(s['c']) != room(c):
                    mv = dict(sess=sess, name=name, frm=room(s['c']), fc=s['c'], L=s['mv'], O=s['f1'],
                              dt=s['del'], to=room(c), at=tick, tc=c, tface=None)
                    out.append(mv)
                    pending[name] = mv
                s['del'] = None
            elif kind == 'del':
                s['del'] = tick
            if nc[0] < 10000:
                if kind in ('walk', 'run') and nc != c:
                    s['mv'] = tick
                s['c'] = nc
            continue
        m = FACE.match(line)
        if m and cur:
            s = st[cur]
            if s['mv'] is not None and (s['f1'] is None or s['f1'][0] <= s['mv']):
                s['f1'] = (tick, m.group(1))
            mv = pending.get(cur)
            if mv and mv['tface'] is None and tick - mv['at'] <= 1:
                mv['tface'] = m.group(1)
            continue
        if line.startswith('    [npc_info'):
            cur = None
    return out


def summary(rs):
    g = collections.defaultdict(list)
    for r in rs:
        g[(r['frm'], r['to'])].append(r)
    for k in sorted(g):
        v = g[k]
        print('== %s -> %s (%d)' % (k[0], k[1], len(v)))
        faced = collections.Counter(re.sub(r'index=\d+, ', '', r['O'][1]) for r in v if r['O'])
        print('  faced before leaving:', dict(faced.most_common(4)))
        land = collections.Counter(tuple(r['tc']) for r in v if (r['tface'] or '').startswith('angle='))
        faces = collections.Counter(r['tface'] for r in v if (r['tface'] or '').startswith('angle='))
        if land:
            xs = [c[0] for c in land]
            zs = [c[1] for c in land]
            print('  fresh landings %d: x %d..%d z %d..%d, faces %s' % (sum(land.values()), min(xs), max(xs), min(zs), max(zs), dict(faces)))
            print('   ', dict(sorted(land.items())))
    first = collections.Counter()
    last = {}
    for r in sorted(rs, key=lambda r: (r['sess'], r['dt'] or 0)):
        if r['frm'] not in ('guardian', 'atrium') or r['to'] in ('lobby', 'ocellus', 'atrium'):
            continue
        if not r['O'] or r['dt'] is None or r['L'] is None or 'coord=' not in r['O'][1]:
            continue
        k = (r['sess'], r['frm'], r['to'])
        if k in last and r['dt'] - last[k] < 15:
            last[k] = r['dt']
            continue
        last[k] = r['dt']
        if r['O'][0] - r['L'] == 1:
            first[r['dt'] - r['O'][0]] += 1
    print('section doors, first raider of each party, del - O:', sorted(first.items()))


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith('--')]
    rs = [mv for p in paths for mv in moves(p)]
    if '--summary' in sys.argv:
        summary(rs)
    else:
        for r in rs:
            print(json.dumps(r))


if __name__ == '__main__':
    main()
