#!/usr/bin/env python3
"""Every splat on the recorder inside each Zulrah fight, with every candidate
cause on that tick, and the prayer that was up when each attack launched.

Output: one line per tick that has splats on the recorder or a landing due.
"""
import sys
import argparse
from collections import defaultdict, Counter
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from zfight import fights, BOSS

CLOUD = 'snakeboss_poisoncloud'


def analyse(path, verbose=True):
    out = []
    for fi, f in enumerate(fights(path)):
        me = f['me']
        idx = f['idx']
        pray = {'magic': 0, 'missiles': 0, 'melee': 0}
        ptile = None
        clouds = {}            # anchor -> placed tick
        landings = defaultdict(list)   # tick -> [(kind, launch tick, prayer at launch, target)]
        minion_types = {}
        boss_form = 'G'
        rows = []
        for t, ev in f['ticks']:
            start = ptile
            for e in ev:
                if e['k'] == 'varbit' and e['name'].startswith('prayer_protectfrom'):
                    pray[e['name'][len('prayer_protectfrom'):]] = e['new']
            for e in ev:
                if e['k'] == 'pl' and e['idx'] == me:
                    start = (e['x'], e['z'])
                    ptile = (e.get('nx', e['x']), e.get('nz', e['z']))
                if e['k'] == 'npc' and e['name'].startswith('snakeboss_minion'):
                    minion_types[e['idx']] = e['name']
                if e['k'] == 'npc' and e['idx'] == idx:
                    boss_form = BOSS.get(e['name'], boss_form)
                if e['k'] == 'locadd' and e['name'] == CLOUD:
                    clouds[(e['x'], e['z'])] = t
                if e['k'] == 'locdel' and (e['x'], e['z']) in clouds:
                    del clouds[(e['x'], e['z'])]
            pr = ''.join({'magic':'G','missiles':'R','melee':'M'}[k] for k, v in pray.items() if v)
            for e in ev:
                if e['k'] == 'proj' and e.get('tkind') == 'player' and e.get('tidx') == me:
                    land = t + (e['et'] + 15) // 30
                    landings[land].append((e['name'].replace('snakeboss_', ''), t, pr, (e['tx'], e['tz'])))
            melee_swings = []
            for e in ev:
                if e['k'] == 'npcx' and e['blk'] == 'sequence' and 'pet_attack' in e['body']:
                    if minion_types.get(e['idx']) == 'snakeboss_minion_melee':
                        melee_swings.append(e['idx'])
            hits = [e['body'] for e in ev if e['k'] == 'plx' and e['idx'] == me and e['blk'] == 'hit']
            # in a cloud at the start of the tick / end of tick
            def incloud(tile):
                if tile is None:
                    return []
                return [a for a in clouds if a[0] <= tile[0] <= a[0] + 2 and a[1] <= tile[1] <= a[1] + 2]
            cs = incloud(start)
            ce = incloud(ptile)
            due = landings.get(t, [])
            rows.append(dict(t=t, rel=t - f['add'], hits=hits, due=due, melee=melee_swings, cs=cs, ce=ce,
                             start=start, end=ptile, pr=pr, form=boss_form))
        out.append((fi, f, rows))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('txt', nargs='+')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    for path in a.txt:
        for fi, f, rows in analyse(path):
            print(f"=== {path.rsplit('/', 1)[-1]} fight {fi}")
            for r in rows:
                if not (r['hits'] or r['due'] or r['melee'] or (a.all and (r['cs'] or r['ce']))):
                    continue
                hs = ' '.join(h.replace('id=', '').replace(', value=', ':') for h in r['hits'])
                due = ' '.join(f"{k}@{lt}[{p}]" for k, lt, p, tg in r['due'])
                print(f"[{r['t']}] +{r['rel']:3d} {r['form']} pr={r['pr']:3s} hits=[{hs}] due=[{due}] "
                      f"melee={len(r['melee'])} cloud_s={len(r['cs'])} cloud_e={len(r['ce'])} {r['start']}->{r['end']}")


if __name__ == '__main__':
    main()
