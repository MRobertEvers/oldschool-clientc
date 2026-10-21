"""Every hit on the recorder in the fought fights, attributed, with protect-missiles state."""
import collections
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS = {4296: [(355, 840)], 4297: [(90, 265), (345, 645), (810, 1185)]}
agg = collections.defaultdict(list)
for s, wins in FIGHTS.items():
    ev = load(A + f'idx{s}.txt')
    pm = 0; pm_at = {}
    spots = collections.defaultdict(list); tents = collections.defaultdict(list); seqs = collections.defaultdict(list)
    for e in ev:
        if e['kind'] == 'varbit' and e['id'] == 'prayer_protectfrommissiles': pm = int(e['new'])
        pm_at[e['tick']] = pm
        if e['who'] == 'me' and e['kind'] == 'spot': spots[e['tick']].append(e['id'])
        if e['kind'] == 'seq' and 'tentacle' in e.get('name', ''): tents[e['tick']].append(e['id'])
        if e['kind'] == 'seq' and 'tfa_guardian' in e.get('name', '') and 'battery' not in e.get('name', ''): seqs[e['tick']].append(e['id'][21:])
    for (t0, t1) in wins:
        for e in ev:
            t = e['tick']
            if not (t0 <= t <= t1) or e['who'] != 'me' or e['kind'] != 'hit': continue
            if e['type'] == '65': src = 'poison'
            elif 'vfx_guardian_ranged_hitanim01' in spots[t]: src = 'ranged'
            else:
                near = [f'G:{x}@{tt-t}' for tt in range(t - 6, t + 1) for x in seqs[tt]] + [f'T:{x[-8:]}@{tt-t}' for tt in range(t - 3, t + 1) for x in tents[tt]]
                src = 'other ' + ' '.join(near[-4:])
            pmv = pm_at.get(t - 4, 0)
            agg[(src.split()[0], pmv)].append(e['value'])
            if not src.startswith('ranged') and not src.startswith('poison'):
                print(s, t, e['type'], e['value'], 'pm', pmv, src)
for k, v in sorted(agg.items()):
    print(k, 'n', len(v), 'max', max(v), 'mean', round(sum(v) / len(v), 1), 'hist', dict(sorted(collections.Counter(v).items())))
