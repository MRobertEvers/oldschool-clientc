#!/usr/bin/env python3
"""How a player's own thrall moves, from --raw RSProx transcripts.

    python3 docs/rsprox-analysis/yama/tools/thrall_moves.py <idx.txt>...

A thrall is the recorder's when it is added within one tile of the recorder (the
cast puts it beside its owner). Every tick of its life is classed by:
  kind     ghost / skeleton / zombie
  engaged  it played its attack seq within the last ENGAGED ticks
  move     idle / walk / run / teleport / telejump (its npc-info movement)
  owner_d  Chebyshev distance to the owner at the start of the tick
and the step's direction against the owner (closer / same / farther).
"""
import collections
import re
import sys

ENGAGED = 6
NPC = re.compile(r'\[(add|walk|run|telejump|teleport|idle|del|crawl)\] npc=\(index=(\d+), id=([a-z_0-9]+) \((\d+)\)'
                 r'(?:, multinpc=[^,]*)?, coord=\((\d+), (\d+), (\d)\)\)(?:, newcoord=\((\d+), (\d+), \d\))?')
PLAYER = re.compile(r'\[(walk|run|teleport|idle|crawl)\] player=\(index=(\d+), name=[^,]*, '
                    r'coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+), \d\))?')
ATTACK = re.compile(r'\[sequence\] id=(ghost_update_tendrill_attack_thrall|skeleton_update_champion_attack_thrall'
                    r'|zombie_update_attack_normal_thrall)')


def kind_of(name):
    for k in ('ghost', 'skeleton', 'zombie'):
        if k in name:
            return k
    return None


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def run(path, stats):
    me_index = None
    tick = -1
    me = None            # owner tile now
    me_start = None      # owner tile at the start of this tick
    cur = None           # ('npc', index) or ('me',) for the block being read
    lives = {}           # index -> life dict
    for line in open(path, errors='replace'):
        if me_index is None:
            m = re.search(r'local player index: (\d+)', line)
            if m:
                me_index = m.group(1)
            continue
        m = re.match(r'\[(\d+)\]', line)
        if m:
            tick = int(m.group(1))
            me_start = me
            continue
        m = PLAYER.search(line)
        if m:
            cur = ('me',) if m.group(2) == me_index else None
            if m.group(2) == me_index:
                me = (int(m.group(5) or m.group(3)), int(m.group(6) or m.group(4)))
            continue
        m = NPC.search(line)
        if m:
            move, index, name = m.group(1), m.group(2), m.group(3)
            p = (int(m.group(5)), int(m.group(6)))
            q = (int(m.group(8)), int(m.group(9))) if m.group(8) else p
            cur = ('npc', index)
            k = kind_of(name) if name.startswith('arceuus_thrall_') else None
            if move == 'add':
                lives.pop(index, None)
                if k and me and cheb(p, me) <= 1:
                    lives[index] = {'kind': k, 'last_attack': -99, 'born': tick}
                continue
            life = lives.get(index)
            if not life:
                continue
            if move == 'del':
                del lives[index]
                continue
            if me_start is None:
                continue
            engaged = tick - life['last_attack'] <= ENGAGED
            d0 = cheb(p, me_start)
            key = (life['kind'], 'engaged' if engaged else 'free')
            stats['ticks'][key] += 1
            stats['move'][key + (move,)] += 1
            if move != 'idle':
                d1 = cheb(q, me_start)
                way = 'closer' if d1 < d0 else ('same' if d1 == d0 else 'farther')
                stats['step'][key + (way,)] += 1
                stats['step_d'][key + (min(d0, 20),)] += 1
                stats['step_len'][key + (move, cheb(p, q))] += 1
            else:
                stats['idle_d'][key + (min(d0, 20),)] += 1
            continue
        if cur and cur[0] == 'npc' and cur[1] in lives and ATTACK.search(line):
            lives[cur[1]]['last_attack'] = tick


def main():
    stats = collections.defaultdict(collections.Counter)
    for path in sys.argv[1:]:
        run(path, stats)
    for key in sorted(stats['ticks']):
        n = stats['ticks'][key]
        moves = {mv: c for (k1, k2, mv), c in stats['move'].items() if (k1, k2) == key}
        print('== %s %s: %d ticks; movement %s' % (key[0], key[1], n, dict(sorted(moves.items(), key=lambda x: -x[1]))))
        ways = {w: c for (k1, k2, w), c in stats['step'].items() if (k1, k2) == key}
        if ways:
            print('   steps vs owner: %s' % ways)
        lens = {(mv, ln): c for (k1, k2, mv, ln), c in stats['step_len'].items() if (k1, k2) == key}
        if lens:
            print('   step kind, tiles: %s' % dict(sorted(lens.items())))
        rows = []
        for d in range(0, 21):
            i = stats['idle_d'][key + (d,)]
            s = stats['step_d'][key + (d,)]
            if i or s:
                rows.append('%d:%d/%d' % (d, i, s))
        print('   owner distance idle/step: ' + ' '.join(rows))


if __name__ == '__main__':
    main()
