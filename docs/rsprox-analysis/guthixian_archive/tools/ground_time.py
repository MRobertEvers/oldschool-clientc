"""Every Archive Guardian ground phase in every recording: how long it stayed down.

    python3 ground_time.py [session ...]

Per fight (tfa_guardian_start's spawn01 to the guardian's del/death): each landing
(idle_to_ground01) to its lift (ground_to_flying01), the tentacle waves (one vine a
raider), damage taken on the ground, and whether the lift was the enrage's.
"""
import sys
from tfa_events import load

A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
SESSIONS = [4294, 4296, 4297, 4298, 4299, 4300, 4301, 4303, 4304, 4305, 4306, 4308,
            4309, 4310, 4311, 4312, 4313, 4314, 4315, 4316]
GUARD = ('tfa_guardian_start', 'tfa_guardian', 'tfa_guardian_unshielded', 'tfa_guardian_dead')


def scan(s):
    ev = load(A + f'idx{s}.txt')
    names = {}
    fights = []
    f = None
    for e in ev:
        w, k, t = e['who'], e['kind'], e['tick']
        if not w.startswith('npc'):
            continue
        if e.get('name'):
            names.setdefault(w, e['name'])
        if k == 'transform':
            names[w] = e['id']
        n = names.get(w, '')
        if n == 'archive_guardian_tentacle' and k == 'add' and f and f['phases'] and f['phases'][-1]['lift'] is None:
            f['phases'][-1]['waves'].setdefault(t, 0)
            f['phases'][-1]['waves'][t] += 1
            continue
        if n == 'tfa_guardian_battery_destroyed' and k == 'transform' and f:
            f['batt'].append(t)
        if n not in GUARD:
            continue
        if k == 'add' and n == 'tfa_guardian_start':
            if f and f['end'] is None:
                f['end'] = ('readd', t)
            f = None
        if (k == 'seq' and e['id'].endswith('spawn01')) or (k == 'add' and n != 'tfa_guardian_start' and (f is None or f['end'] is not None)):
            f = dict(start=t, who=w, phases=[], end=None, enrage=[], batt=[], late=k == 'add')
            fights.append(f)
        if f is None or w != f['who']:
            continue
        if k == 'seq' and 'idle_to_ground01' in e['id']:
            f['phases'].append(dict(land=t, lift=None, waves={}, dmg=0))
        elif k == 'seq' and 'ground_to_flying01' in e['id']:
            if f['phases'] and f['phases'][-1]['lift'] is None:
                f['phases'][-1]['lift'] = t
        elif k == 'seq' and 'enraged_phase01' in e['id']:
            f['enrage'].append(t)
        elif k == 'hit' and f['phases'] and f['phases'][-1]['lift'] is None and e['type'] in ('16', '17', '43'):
            f['phases'][-1]['dmg'] += e['value']
        elif k == 'del' and f['end'] is None:
            f['end'] = ('del', t)
        elif k == 'transform' and e['id'] == 'tfa_guardian_dead' and f['end'] is None:
            f['end'] = ('dead', t)
    return fights


for s in [int(a) for a in sys.argv[1:]] or SESSIONS:
    for i, f in enumerate(scan(s)):
        print(f'{s} fight{i + 1} {"JOINED" if f["late"] else "spawn"} {f["start"]} end {f["end"]} enrage-seq {f["enrage"][:3]} batteries {f["batt"]}')
        for j, p in enumerate(f['phases']):
            w = p['waves']
            raiders = sorted(set(w.values()))
            lift = p['lift']
            dur = lift - p['land'] if lift else None
            en = lift is not None and any(lift <= x <= lift + 12 for x in f['enrage'])
            wt = sorted(w)
            print(f'    ground{j + 1} land {p["land"]} lift {lift} down {dur}{" ENRAGE" if en else ""} '
                  f'waves {len(w)} vines/wave {raiders} first+{wt[0] - p["land"] if wt else None} '
                  f'last+{wt[-1] - p["land"] if wt else None} dmg {p["dmg"]}')
