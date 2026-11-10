#!/usr/bin/env python3
"""yama_audit.py <rundir> [<rundir> ...] -- the Yama solver's PASS row, from each run's tick log.

For each run prints: killed (despawn seq 12161 on Yama), both Judges died (npc_free of 14180 after
a real hit), hits on the Judges, the player's deaths, food/potions used, and the regulars whose
landing tick found the wrong protection prayer on (telegraphed: Yama's seq 12144 + spot 3246/3243
on the attack tick; hit at A+3). Rows of `ticklog.tsv` are read by position (see sum.py).
"""
import csv
import sys

FIRE, SHADOW = 3246, 3243
MASK_MAGIC, MASK_MISSILES = 4096, 8192


def audit(run):
    rows = []
    with open(run + '/ticklog.tsv', errors='replace') as f:
        for line in f.read().split('\n')[1:]:
            r = line.split('\t')
            if len(r) > 2:
                rows.append(r)
    anim = {}
    spot = {}
    pr = {}
    hp = {}
    regulars = []
    deaths = 0
    judge_hits = 0
    food = 0
    pots = 0
    killed = None
    judges = 0
    hits_player = []
    for r in rows:
        kind = r[2]
        t = int(r[1])
        if kind == 'npc_anim' and r[4] == '14176':
            anim[t] = r[5]
            if r[5] == '12161' and killed is None:
                killed = t
        elif kind == 'npc_spotanim' and r[4] == '14176':
            spot.setdefault(t, []).append(int(r[5]))
        elif kind == 'raider':
            hp[t] = int(r[4])
            pr[t] = int(r[6])
        elif kind == 'hit_npc' and r[4] == '14180' and int(r[5]) > 0:
            judge_hits += 1
        elif kind == 'npc_free' and r[4] == '14180':
            judges += 1
        elif kind == 'hit_player':
            hits_player.append((t, int(r[5])))
        elif kind == 'consume':
            if 'manta' in r[9]:
                food += 1
            elif 'prayer' in r[9] or '2restore' in r[9]:
                pots += 1
        elif kind == 'death' or (kind == 'mark' and 'died' in ' '.join(r)):
            deaths += 1
    wrong = 0
    off = 0
    n = 0
    for a in sorted(anim):
        if anim[a] != '12144' or a not in spot:
            continue
        style = None
        if FIRE in spot[a]:
            style = MASK_MAGIC
        elif SHADOW in spot[a]:
            style = MASK_MISSILES
        if style is None:
            continue
        n += 1
        m = pr.get(a + 3)
        if m is None:
            continue
        if m & style:
            continue
        if m & (MASK_MAGIC | MASK_MISSILES):
            wrong += 1
        else:
            off += 1
    return {'run': run.rstrip('/').split('/')[-1], 'killed': killed, 'judges': judges, 'judge_hits': judge_hits,
            'regulars': n, 'wrong': wrong, 'off': off, 'food': food, 'pots': pots,
            'damage': sum(d for t, d in hits_player), 'end': max(hp) if hp else 0}


def main():
    w = csv.writer(sys.stdout, delimiter='\t')
    w.writerow(['run', 'killed', 'judges', 'judge_hits', 'regulars', 'wrong', 'off', 'food', 'pots', 'damage'])
    for run in sys.argv[1:]:
        try:
            a = audit(run)
        except OSError as e:
            print(run, 'unreadable', e)
            continue
        w.writerow([a[k] for k in ('run', 'killed', 'judges', 'judge_hits', 'regulars', 'wrong', 'off', 'food', 'pots', 'damage')])


if __name__ == '__main__':
    main()
