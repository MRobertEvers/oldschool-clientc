#!/usr/bin/env python3
"""Our tick log's Zulrah fight as the same per-phase summary zphases.py prints
for a recording, in template coordinates (the instance offset taken from the
boss's spawn tile, which is the middle position's SW (2266, 3073))."""
import sys
from collections import defaultdict

BOSS = {2042: 'G', 2043: 'R', 2044: 'B'}
POS = {(2266, 3073): 'M', (2276, 3071): 'E', (2266, 3062): 'S', (2256, 3071): 'W'}
SEQ = {5071: 'spawn', 5073: 'emerge', 5072: 'sink', 5069: 'acid1', 5068: 'acid3', 5806: 'tailL', 5807: 'tailR',
       5804: 'death', 5808: 'defend'}


def xz(c):
    return (c >> 14) & 0x3FFF, c & 0x3FFF


def load(path):
    rows = []
    for line in open(path, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) < 11 or not f[1].isdigit():
            continue
        v = [int(x) if x.lstrip('-').isdigit() else 0 for x in f[3:9]]
        rows.append((int(f[1]), f[2], v, f[9]))
    return rows


def main(path, detail=False):
    rows = load(path)
    boss_slot = None
    off = None
    for t, k, v, lab in rows:
        if k == 'npc_spawn' and v[1] == 2042:
            boss_slot = v[0]
            x, z = xz(v[2])
            off = (x - 2266, z - 3073)
            add = t
            break
    if boss_slot is None:
        print('no boss spawn')
        return
    form = 'G'
    pos = (2266, 3073)
    phases = []
    cur = None
    by_tick = defaultdict(list)
    for r in rows:
        by_tick[r[0]].append(r)
    for t in sorted(by_tick):
        evs = by_tick[t]
        for tt, k, v, lab in evs:
            if k == 'npc_tile' and v[0] == boss_slot:
                pos = (v[1] - off[0], v[2] - off[1])
            if k == 'npc_retype' and v[0] == boss_slot:
                form = BOSS.get(v[2], form)
        seqs = [SEQ.get(v[2], v[2]) for tt, k, v, lab in evs if k == 'npc_anim' and v[0] == boss_slot]
        if 'spawn' in seqs or 'emerge' in seqs:
            cur = dict(t0=t, form=form, pos=POS.get(pos, pos), acts=[], end=None, faces=[])
            phases.append(cur)
        if cur is None:
            continue
        rel = t - cur['t0']
        for s in seqs:
            if s == 'sink':
                cur['end'] = ('sink', rel)
            elif s == 'death':
                cur['end'] = ('death', rel)
            elif s in ('tailL', 'tailR'):
                cur['acts'].append((rel, 'T', s[-1]))
        projs = [v for tt, k, v, lab in evs if k == 'projectile' and v[3] in (1044, 1045, 1046, 1047)]
        clouds = [v for v in projs if v[3] == 1045]
        if clouds:
            cur['acts'].append((rel, 'C', tuple((xz(c[1])[0] - off[0], xz(c[1])[1] - off[1]) for c in clouds)))
        for p in projs:
            d = xz(p[1])
            d = (d[0] - off[0], d[1] - off[1])
            s = xz(p[0])
            s = (s[0] - off[0], s[1] - off[1])
            if p[3] == 1047:
                cur['acts'].append((rel, 'E', d))
            elif p[3] == 1044:
                cur['acts'].append((rel, 'r', (s, d)))
            elif p[3] == 1046:
                cur['acts'].append((rel, 'm', (s, d)))
        for tt, k, v, lab in evs:
            if k == 'npc_face' and v[0] == boss_slot:
                if v[2] == -2:
                    cur['faces'].append((rel, 'A%d' % v[3]))
                elif v[2] == -1:
                    cur['faces'].append((rel, 'reset' if v[3] < 0 else 'P'))
    print(f"boss slot {boss_slot} add {add} offset {off}")
    for j, ph in enumerate(phases):
        summ = ''.join(a[1] for a in ph['acts'])
        offs = ','.join(str(a[0]) for a in ph['acts'])
        print(f"  p{j + 1:2d} [{ph['t0']}] +{ph['t0'] - add:4d} {ph['form']}@{ph['pos']} {summ:14s} offs={offs} end={ph['end']} faces={ph['faces'][:12]}")
        if detail:
            for a in ph['acts']:
                print('        +%3d %s %s' % a)


if __name__ == '__main__':
    main(sys.argv[1], '--detail' in sys.argv)
