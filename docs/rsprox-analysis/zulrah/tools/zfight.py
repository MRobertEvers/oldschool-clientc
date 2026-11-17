#!/usr/bin/env python3
"""Zulrah fight timelines from an RSProx transcript.

usage: zfight.py <txt> [--fight N] [--all] [--player]

One line per tick that carries anything of the boss's: position/form, seq,
face, transformation, projectiles it launched, clouds, eggs, snakelings, hits
on the recorder, prayer state.
"""
import sys
import os
import argparse
import re
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from zparse import parse, local_index

BOSS = {'snakeboss_boss_ranged': 'G', 'snakeboss_boss_melee': 'R', 'snakeboss_boss_magic': 'B'}
MIN = {'snakeboss_minion_melee', 'snakeboss_minion_magic', 'snakeboss_minion_dying'}
SNDN = {}


def load_sound_names():
    import os
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..',
                     'OSRS-Content', 'osrs-content', 'pack', '4_soundeffects.pack')
    try:
        for line in open(p):
            line = line.strip()
            if '=' in line:
                a, b = line.split('=', 1)
                if a.isdigit():
                    SNDN[int(a)] = b
    except OSError:
        pass


def fights(path):
    """Return a list of fights: dict(add_tick, del_tick, idx, ticks=[(t, ev)])."""
    me = local_index(path)
    out = []
    cur = None
    for t, ev in parse(path):
        for e in ev:
            if e['k'] == 'npc' and e['name'] in BOSS and e['mv'] == 'add':
                cur = dict(add=t, idx=e['idx'], ticks=[], dele=None, me=me)
                out.append(cur)
        if cur is not None:
            cur['ticks'].append((t, ev))
            for e in ev:
                if e['k'] == 'npc' and e['idx'] == cur['idx'] and e['mv'] == 'del':
                    cur['dele'] = t
            # keep collecting 12 ticks past the boss del (loot, messages)
            if cur['dele'] is not None and t > cur['dele'] + 12:
                cur = None
            # or until the player leaves (teleport far away)
    # translate raw instance tiles to template tiles: the boss always adds at
    # the middle, SW (2266, 3073)
    for f in out:
        add = None
        for t, ev in f['ticks']:
            for e in ev:
                if e['k'] == 'npc' and e['idx'] == f['idx'] and e['mv'] == 'add':
                    add = (e['x'], e['z'])
            if add:
                break
        ox, oz = add[0] - 2266, add[1] - 3073
        f['off'] = (ox, oz)
        if ox == 0 and oz == 0:
            continue
        for t, ev in f['ticks']:
            for e in ev:
                for kx, kz in (('x', 'z'), ('nx', 'nz'), ('sx', 'sz'), ('tx', 'tz')):
                    if kx in e and e[kx] > 6000:
                        e[kx] -= ox
                        e[kz] -= oz
                if 'body' in e and 'coord=(' in e['body']:
                    def fix(m):
                        x, z = int(m.group(1)), int(m.group(2))
                        if x > 6000:
                            x -= ox; z -= oz
                        return f'coord=({x}, {z}, {m.group(3)})'
                    e['body'] = re.sub(r'coord=\((\d+), (\d+), (\d+)\)', fix, e['body'])
    return out


def fmt_fight(f, verbose=False, show_player=True):
    idx = f['idx']
    me = f['me']
    lines = []
    form = 'G'
    pos = None
    t0 = f['add']
    minions = {}
    for t, ev in f['ticks']:
        parts = []
        for e in ev:
            k = e['k']
            if k == 'npc' and e['idx'] == idx:
                form = BOSS.get(e['name'], form)
                if e['mv'] == 'add':
                    pos = (e['x'], e['z'])
                    parts.append(f"ADD {form}@{pos} {e.get('spawnangle')}")
                elif e['mv'] == 'telejump':
                    pos = (e['nx'], e['nz'])
                    parts.append(f"TJ {e['x']},{e['z']}->{pos}")
                elif e['mv'] == 'del':
                    parts.append('DEL')
                elif e['mv'] != 'idle':
                    parts.append(f"{e['mv']}")
            elif k == 'npcx' and e['idx'] == idx:
                b = e['blk']
                body = e['body']
                if b == 'sequence':
                    body = body.replace('snakeboss_', '').replace(' (5069)', '').replace(' (5808)', '')
                    if 'defend' in body and not verbose:
                        continue
                    parts.append('seq ' + body)
                elif b in ('hit', 'headbar') and not verbose:
                    continue
                elif b == 'transformation':
                    parts.append('->' + body.split()[0].split('=')[1].replace('snakeboss_boss_', ''))
                elif b == 'spotanim' and not verbose and ('impact' in body):
                    continue
                else:
                    parts.append(f"{b} {body}")
            elif k == 'npc' and e['name'] in MIN:
                if e['mv'] == 'add':
                    minions[e['idx']] = e['name']
                    parts.append(f"+{e['name'].replace('snakeboss_minion_', 'm')}@{e['x']},{e['z']} {e.get('spawnangle')}")
                elif e['mv'] == 'del':
                    parts.append(f"-{e['name'].replace('snakeboss_minion_', 'm')}#{e['idx']}@{e['x']},{e['z']}")
            elif k == 'proj':
                if e['name'] in ('steel_dart_travel', 'fireblast_travel', 'snakeboss_minion_spell') and not verbose:
                    continue
                if e['name'].startswith('league05') and not verbose:
                    continue
                tgt = f"{e.get('tx')},{e.get('tz')}"
                if e.get('tkind'):
                    tgt += '/' + e['tkind'][0]
                parts.append(f"P:{e['name'].replace('snakeboss_', '')} {e.get('sx')},{e.get('sz')}->{tgt} {e['st']}/{e['et']} a{e['ang']} p{e['prog']} h{e['sh']}->{e['eh']}")
            elif k == 'locadd' and 'snakeboss' in e['name']:
                parts.append(f"LOC+{e['name'].replace('snakeboss_', '')}@{e['x']},{e['z']} r{e['rot']}")
            elif k == 'locdel':
                parts.append(f"LOC-@{e['x']},{e['z']} r{e['rot']}")
            elif k == 'sarea':
                if e['id'] in (224, 2384, 2396, 155, 156, 227) and not verbose:
                    continue
                parts.append(f"SA{e['id']}:{SNDN.get(e['id'], '')} r{e['range']} d{e['delay']} @{e['x']},{e['z']}")
            elif k == 'synth' and verbose:
                parts.append(f"SY{e['id']}:{SNDN.get(e['id'], '')} d{e['delay']}")
            elif k == 'plx' and e['idx'] == me and show_player:
                if e['blk'] == 'hit':
                    parts.append('HIT ' + e['body'])
            elif k == 'pl' and e['idx'] == me and show_player:
                if e['mv'] != 'idle':
                    nc = (e.get('nx'), e.get('nz'))
                    parts.append(f"me {e['mv']} {e['x']},{e['z']}->{nc[0]},{nc[1]}")
            elif k == 'varbit' and e['name'] in ('prayer_protectfrommagic', 'prayer_protectfrommissiles', 'prayer_protectfrommelee'):
                parts.append(f"{e['name'].replace('prayer_protectfrom', 'pp_')}={e['new']}")
            elif k == 'msg' and e['type'] not in ('spam',):
                parts.append(f"MSG[{e['type']}] {e['text'][:80]}")
            elif k == 'midi':
                parts.append(f"{e['pkt']} {e['body'][:60]}")
            elif k == 'cam' and verbose:
                parts.append(f"{e['pkt']} {e['body']}")
        if parts:
            lines.append(f"[{t}] +{t - t0:4d} {form} " + ' | '.join(parts))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('txt')
    ap.add_argument('--fight', type=int, default=None)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--summary', action='store_true')
    a = ap.parse_args()
    load_sound_names()
    fs = fights(a.txt)
    if a.summary:
        for i, f in enumerate(fs):
            print(i, f['add'], f['dele'], (f['dele'] or 0) - f['add'])
        return
    for i, f in enumerate(fs):
        if a.fight is not None and i != a.fight:
            continue
        print(f"=== fight {i}: add {f['add']} del {f['dele']}")
        for l in fmt_fight(f, a.verbose):
            print(l)


if __name__ == '__main__':
    main()
