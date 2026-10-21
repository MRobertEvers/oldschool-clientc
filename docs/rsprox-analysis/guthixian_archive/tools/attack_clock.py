"""The guardian's action stream per fight, coded: R ranged, S special, W swoosh, M melee,
E enrage, G to-ground, F to-flying. Prints the stream with gaps and gap stats per pair."""
import collections
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS = {4294: [(540, 1050), (1095, 1495)], 4296: [(355, 840)], 4297: [(90, 265), (345, 645), (810, 1185)]}
CODE = {'range_attack01': 'R', 'special_attack01': 'S', 'swoosh_attack01': 'W', 'melee_attack01': 'M',
        'enraged_phase01': 'E', 'idle_to_ground01': 'G', 'ground_to_flying01': 'F', 'death01': 'D', 'spawn01': '*', 'idle01': 'I'}
pairs = collections.Counter()
for s, wins in FIGHTS.items():
    ev = load(A + f'idx{s}.txt')
    for (t0, t1) in wins:
        st = []
        for e in ev:
            if t0 <= e['tick'] <= t1 and e['kind'] == 'seq' and e['who'].startswith('npc') and 'tfa_guardian' in e.get('name', '') and 'battery' not in e.get('name', ''):
                c = CODE.get(e['id'].replace('npc_archive_guardian_', ''), '?')
                st.append((e['tick'], c))
        out = []
        for i, (t, c) in enumerate(st):
            g = t - st[i - 1][0] if i else 0
            out.append(f'{c}{g}')
            if i: pairs[(st[i - 1][1], c, g)] += 1
        print(f'{s} [{t0}..{t1}] ' + ' '.join(out))
print()
for (a, b, g), n in sorted(pairs.items()):
    print(f'{a}->{b} +{g}: {n}')
