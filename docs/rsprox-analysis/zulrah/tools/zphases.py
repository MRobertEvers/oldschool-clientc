#!/usr/bin/env python3
"""Per-phase summaries of every Zulrah fight in the given transcripts.

A phase starts at the boss's add (snakeboss_spawn) or an emergefast, and ends
at the next sinkfast (or death). Each action: offset from the phase start,
kind, detail.

  C  cloud barrage (double_orb pair): targets
  E  snakeling egg: target
  r  ranged attack (snakeboss_orb at the player)
  m  magic attack (snakeboss_fireball at the player)
  T  tail swing (attack_tail_left/right)
  F  face_pathingentity player (no attack)
"""
import sys
import argparse
import re
from collections import Counter, defaultdict
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from zfight import fights, BOSS

POS = {(2266, 3073): 'M', (2276, 3071): 'E', (2266, 3062): 'S', (2256, 3071): 'W'}


def phases_of(f):
    idx = f['idx']
    out = []
    cur = None
    form = 'G'
    pos = None
    for t, ev in f['ticks']:
        boss_blocks = [e for e in ev if e['k'] == 'npcx' and e['idx'] == idx]
        boss_hdr = [e for e in ev if e['k'] == 'npc' and e['idx'] == idx]
        for h in boss_hdr:
            form = BOSS.get(h['name'], form)
            if h['mv'] == 'add':
                pos = (h['x'], h['z'])
            elif h['mv'] == 'telejump':
                pos = (h['nx'], h['nz'])
        seqs = [b['body'] for b in boss_blocks if b['blk'] == 'sequence']
        trans = [b['body'] for b in boss_blocks if b['blk'] == 'transformation']
        newform = form
        for tr in trans:
            nm = tr.split()[0].split('=')[1]
            newform = BOSS.get(nm, form)
        starts = any('snakeboss_spawn' in s or 'emergefast' in s for s in seqs)
        if starts:
            cur = dict(t0=t, form=newform, pos=POS.get(pos, str(pos)), acts=[], end=None, endkind=None,
                       faces=[])
            out.append(cur)
        form = newform
        if cur is None:
            continue
        rel = t - cur['t0']
        for b in boss_blocks:
            if b['blk'] == 'face_angle' and not starts:
                cur['faces'].append((rel, 'A' + b['body'].split('=')[1]))
            if b['blk'] == 'face_pathingentity':
                if 'entity=null' in b['body']:
                    cur['faces'].append((rel, 'reset'))
                else:
                    cur['faces'].append((rel, 'P'))
        for s in seqs:
            if 'sinkfast' in s:
                cur['end'] = rel
                cur['endkind'] = 'sink'
            elif 'snakeboss_death' in s:
                cur['end'] = rel
                cur['endkind'] = 'death'
            elif 'attack_tail' in s:
                cur['acts'].append((rel, 'T', 'L' if 'left' in s else 'R'))
        projs = [e for e in ev if e['k'] == 'proj' and e['name'] in (
            'snakeboss_double_orb', 'snakeboss_egg', 'snakeboss_orb', 'snakeboss_fireball')]
        clouds = [p for p in projs if p['name'] == 'snakeboss_double_orb']
        if clouds:
            cur['acts'].append((rel, 'C', tuple((p['tx'], p['tz']) for p in clouds)))
        for p in projs:
            if p['name'] == 'snakeboss_egg':
                cur['acts'].append((rel, 'E', (p['tx'], p['tz'])))
            elif p['name'] == 'snakeboss_orb':
                cur['acts'].append((rel, 'r', (p['tx'], p['tz'])))
            elif p['name'] == 'snakeboss_fireball':
                cur['acts'].append((rel, 'm', (p['tx'], p['tz'])))
        # an attack seq with no projectile at all
        if any('attack_acidx1' in s for s in seqs) and not projs:
            cur['acts'].append((rel, '?', None))
    return out


def summary(ph):
    s = ''
    for rel, k, d in ph['acts']:
        s += k
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('txt', nargs='+')
    ap.add_argument('--detail', action='store_true')
    a = ap.parse_args()
    for path in a.txt:
        for i, f in enumerate(fights(path)):
            print(f"=== {path.rsplit('/', 1)[-1]} fight {i} add {f['add']} del {f['dele']}")
            for j, ph in enumerate(phases_of(f)):
                offs = ','.join(str(r) for r, k, d in ph['acts'])
                print(f"  p{j + 1:2d} [{ph['t0']}] +{ph['t0'] - f['add']:4d} {ph['form']}@{ph['pos']} "
                      f"{summary(ph):14s} offs={offs} end={ph['endkind']}@{ph['end']} faces={ph['faces'][:12]}")
                if a.detail:
                    for rel, k, d in ph['acts']:
                        print(f"        +{rel:3d} {k} {d}")


if __name__ == '__main__':
    main()
