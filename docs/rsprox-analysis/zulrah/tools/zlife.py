#!/usr/bin/env python3
"""Snakeling lives, recorded against ours: per life, every npc-info block by
offset from the add (face kinds, sequences, hits, ops, transformation, level),
the movement steps, and how it ended.

  zlife.py rec <transcript>...       recorded lives
  zlife.py ours <ticklog.tsv>...     our lives
"""
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import zparse  # noqa: E402

SNAKE = {2045: 'melee', 2046: 'magic', 2047: 'dying'}
SEQ = {2413: 'spawn', 1741: 'attack', 2408: 'death', 1742: 'defend'}


def rec_lives(paths):
    lives = []
    for path in paths:
        open_ = {}
        for t, ev in zparse.parse(path):
            for e in ev:
                if e['k'] == 'npc' and e['id'] in SNAKE:
                    if e['mv'] == 'add':
                        open_[e['idx']] = dict(src=path.rsplit('/', 1)[-1], t0=t, type=SNAKE[e['id']], ev=[],
                                               steps=0, end=None)
                    life = open_.get(e['idx'])
                    if life is None:
                        continue
                    if e['mv'] in ('walk', 'run', 'crawl'):
                        life['steps'] += 1
                        life['ev'].append((t - life['t0'], e['mv']))
                    if e['mv'] == 'del':
                        life['end'] = t - life['t0']
                        lives.append(life)
                        del open_[e['idx']]
                elif e['k'] == 'npcx' and e['idx'] in open_:
                    life = open_[e['idx']]
                    b = e['blk']
                    body = e['body']
                    if b == 'sequence':
                        sid = int(body.split('(')[1].split(')')[0]) if '(' in body else -1
                        val = SEQ.get(sid, str(sid))
                    elif b in ('face_pathingentity', 'facing', 'face_angle'):
                        val = 'reset' if ('-1' in body or 'null' in body or 'none' in body.lower() or 'index=65535' in body) else 'P'
                        if b == 'face_angle':
                            val = 'A'
                    elif b == 'hit':
                        val = body.replace('id=', '').replace(' value=', '')
                    elif b == 'enabled_ops':
                        val = body.split('=')[-1]
                    elif b == 'transformation':
                        val = body.split('(')[-1].rstrip(')')
                    elif b == 'level_change':
                        val = body.split('=')[-1]
                    else:
                        val = ''
                    life['ev'].append((t - life['t0'], b + (':' + val if val else '')))
    return lives


def our_lives(paths):
    lives = []
    for path in paths:
        open_ = {}
        for line in open(path, errors='replace'):
            f = line.rstrip('\n').split('\t')
            if len(f) < 10 or not f[1].lstrip('-').isdigit() or f[0] == 'ticklog-v1':
                continue
            t, k = int(f[1]), f[2]
            try:
                slot = int(f[3])
            except ValueError:
                continue
            if k == 'npc_spawn' and int(f[4]) in SNAKE:
                open_[slot] = dict(src=path, t0=t, type=SNAKE[int(f[4])], ev=[], steps=0, end=None)
                continue
            life = open_.get(slot)
            if life is None:
                continue
            o = t - life['t0']
            if k == 'npc_anim':
                life['ev'].append((o, 'sequence:' + SEQ.get(int(f[5]), f[5])))
            elif k == 'npc_face':
                if f[5] == '-2':
                    life['ev'].append((o, 'face_angle:A'))
                else:
                    life['ev'].append((o, 'face_pathingentity:' + ('reset' if f[6] == '-1' else 'P')))
            elif k == 'npc_info':
                what = f[9].split(' ')[0]
                if what == 'ops':
                    life['ev'].append((o, 'enabled_ops:0b{:05b}'.format(~int(f[5]) & 31)))
                elif what == 'level':
                    life['ev'].append((o, 'level_change:' + f[5]))
            elif k == 'npc_retype':
                life['ev'].append((o, 'transformation:' + f[5]))
            elif k == 'hit_npc':
                life['ev'].append((o, 'hit'))
            elif k == 'npc_tile':
                life['steps'] += 1
                life['ev'].append((o, 'step'))
            elif k == 'npc_free':
                life['end'] = o
                lives.append(life)
                del open_[slot]
    return lives


def summary(lives):
    by = defaultdict(list)
    for lf in lives:
        by[lf['type']].append(lf)
    for typ, ls in sorted(by.items()):
        print(f"== {typ}: {len(ls)} lives")
        kinds = Counter()
        firsts = Counter()
        att_gaps = Counter()
        att_first = Counter()
        ends = Counter()
        faces = Counter()
        for lf in ls:
            seen = set()
            atts = [o for o, k in lf['ev'] if k == 'sequence:attack']
            for o, k in lf['ev']:
                kinds[k.split(':')[0]] += 1
                if o == 0:
                    firsts[k] += 1 if k not in seen else 0
                    seen.add(k)
            if atts:
                att_first[atts[0]] += 1
                for a, b in zip(atts, atts[1:]):
                    att_gaps[b - a] += 1
            fs = ' '.join('%d%s' % (o, k.split(':')[1][0]) for o, k in lf['ev'] if k.startswith('face_'))
            faces[fs if len(fs) < 40 else fs[:40] + '...'] += 1
            died = [o for o, k in lf['ev'] if k == 'sequence:death']
            ends['death seq at %s, del at +%s after it' % (died[0] if died else '-',
                                                            (lf['end'] - died[0]) if died and lf['end'] is not None else '-')] += 1
        print('   blocks per kind:', dict(kinds))
        print('   at add:', dict(firsts))
        print('   first attack offset:', dict(sorted(att_first.items())))
        print('   attack gaps:', dict(sorted(att_gaps.items())))
        print('   face patterns:', faces.most_common(8))
        print('   ends:', ends.most_common(6))


if __name__ == '__main__':
    mode, paths = sys.argv[1], sys.argv[2:]
    summary(rec_lives(paths) if mode == 'rec' else our_lives(paths))
