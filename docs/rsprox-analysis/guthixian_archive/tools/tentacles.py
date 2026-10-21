"""Tentacles: spawn tile vs the player's recent tiles, attack ticks, and hits on the player."""
import collections
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
rel = collections.Counter(); life = collections.Counter(); hitrel = collections.Counter(); missrel = collections.Counter()
for s in (4296, 4297):
    ev = load(A + f'idx{s}.txt')
    me_at = {}; me = None
    for e in ev:
        if e['who'] == 'me' and e['kind'] in ('tile', 'move'): me = e['at']
        me_at[e['tick']] = me
    tent = {}
    hits = collections.defaultdict(list)
    for e in ev:
        if e['who'] == 'me' and e['kind'] == 'hit' and e['type'] in ('16', '12') : hits[e['tick']].append(e['value'])
    for e in ev:
        if 'tentacle' not in e.get('name', ''): continue
        w = e['who']; t = e['tick']
        if e['kind'] == 'add':
            tent[w] = dict(at=e['at'], spawn=t, attacks=[])
            for back in (1, 2, 3):
                m = me_at.get(t - back)
                if m: rel[(back, e['at'][0] - m[0], e['at'][1] - m[1])] += 1
        elif e['kind'] == 'seq' and w in tent:
            if 'attack' in e['id']:
                tent[w]['attacks'].append(t)
                m = me_at.get(t - 1)
                d = (e and m and (m[0] - tent[w]['at'][0], m[1] - tent[w]['at'][1]))
                big = [v for v in hits.get(t, []) if v >= 11]
                (hitrel if big else missrel)[d] += 1
            elif 'despawn' in e['id']:
                tent[w]['despawn'] = t
        elif e['kind'] == 'del' and w in tent:
            T = tent.pop(w)
            life[(tuple(a - T['spawn'] for a in T['attacks']), T.get('despawn', 0) - T['spawn'], t - T['spawn'])] += 1
print('spawn tile - player tile (t-back):')
for back in (1, 2, 3):
    print(' back', back, sorted([(k[1:], v) for k, v in rel.items() if k[0] == back], key=lambda x: -x[1])[:10])
print('lifecycles (attack offsets, despawn seq offset, del offset):', life.most_common(8))
print('player - tentacle at attack (t-1) when a big hit landed:', hitrel.most_common(12))
print('...when no big hit:', missrel.most_common(25))
