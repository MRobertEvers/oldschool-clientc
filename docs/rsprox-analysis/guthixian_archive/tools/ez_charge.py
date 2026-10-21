"""Ezekiel's swords and their charge: per special, each sword's charge (type-23 splats,
the 80-point bar) when it flew, whether its lane took his south-west tile, and what he
did after (weaken / heal, his splats, his seqs).

    python3 ez_charge.py <all-players txt> [--casts]
"""
import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
ev = load(sys.argv[1]); names = {}
for e in ev:
    if 'name' in e and e['who'].startswith('npc:'): names.setdefault(e['who'], e['name'])
byt = collections.defaultdict(list)
for e in ev: byt[e['tick']].append(e)
bpos = None; hp = None; mx = None
bpos_at = {}; hp_at = {}
swords = {}; boss = []
for t in sorted(byt):
    for e in byt[t]:
        w = e['who']; nm = names.get(w, '')
        if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move', 'tile'): bpos = e['at']
        if e['kind'] == 'varbit' and e.get('id') == 'hpbar_hud_hp': hp = e['new']
        if nm == 'tfa_ezekiel_sword':
            s = swords.setdefault(w, dict(hits=[], spots=[]))
            if e['kind'] == 'add': s.update(add=t, at=e['at'])
            if e['kind'] == 'move': s.update(fly=t, to=e['at'])
            if e['kind'] == 'hit': s['hits'].append((t, e.get('type'), e.get('value')))
            if e['kind'] == 'spot': s['spots'].append((t, e['id'], e.get('delay')))
            if e['kind'] == 'del': s['del'] = t
        if nm == 'tfa_ezekiel':
            if e['kind'] == 'hit': boss.append((t, 'hit', f"{e.get('type')}:{e.get('value')}"))
            if e['kind'] == 'seq' and e['id'] != '-1': boss.append((t, 'seq', e['id'].replace('npc_ezekiel_', '')))
            if e['kind'] == 'say': boss.append((t, 'say', e['raw'][5:]))
            if e['kind'] == 'spot' and 'sang' not in e['id'] and 'thrall' not in e['id']: boss.append((t, 'spot', e['id']))
    bpos_at[t] = bpos; hp_at[t] = hp
def at(d, t):
    ks = [k for k in d if k <= t]
    return d[max(ks)] if ks else None
groups = collections.defaultdict(list)
for w, s in swords.items():
    if 'add' in s: groups[s['add']].append((w, s))
for S in sorted(groups):
    print(f"== S={S}")
    flies = []
    for w, s in sorted(groups[S], key=lambda x: (x[1].get('fly', 0), x[1]['at'])):
        f = s.get('fly'); b = at(bpos_at, f) if f else None
        if f:
            col = s['to'][0] == s['at'][0]
            lane = s['at'][0] if col else s['at'][1]
            through = b and ((col and b[0] == lane) or (not col and b[1] == lane))
        else:
            col = None; lane = None; through = False
        charge = sum(v for (t, ty, v) in s['hits'] if str(ty) == '23' and t <= (f or 10**9))
        late = sum(v for (t, ty, v) in s['hits'] if str(ty) == '23' and f and t > f)
        splash = [t for (t, i, d) in s['spots'] if i == 'failedspell_impact']
        print(f"   sword {w[4:]} {s['at'][:2]} {'col' if col else 'row'} {lane} fly {f} (S+{f - S if f else '-'}) charge {charge}/80" + (f" (+{late} after the flight)" if late else '') + (f" splashed {splash}" if splash else '') + ('  THROUGH HIM' if through else ''))
        if f: flies.append(f)
    if flies:
        f0 = min(flies); f1 = max(flies)
        for (t, k, x) in boss:
            if f0 - 1 <= t <= f1 + 13: print(f"      [{t}] +{t - f0} {k} {x}  hp={at(hp_at, t)}")
