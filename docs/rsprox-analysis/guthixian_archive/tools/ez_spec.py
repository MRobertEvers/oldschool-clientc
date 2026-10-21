"""Ezekiel, per special: his sw tile, the raiders, each sword lane, the outcome (weaken/heal).

    python3 ez_spec.py <all-players txt>
"""
import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
path = sys.argv[1]
ev = load(path)
names = {}
for e in ev:
    if 'name' in e and e['who'].startswith('npc:'):
        names.setdefault(e['who'], e['name'])
byt = collections.defaultdict(list)
for e in ev: byt[e['tick']].append(e)
bpos = None; ppos = {}; hp = None
hist = {}   # tick -> (bpos, dict(ppos), hp)
specials = []; swords = {}; events = []
for t in sorted(byt):
    for e in byt[t]:
        w = e['who']; nm = names.get(w, '')
        if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move', 'tile'): bpos = e['at']
        if w.startswith('pl:') or w == 'me':
            if e['kind'] in ('move', 'tile') and e.get('at'): ppos[w] = e['at']
        if e['kind'] == 'varbit' and e.get('id') == 'hpbar_hud_hp': hp = e['new']
        if nm == 'tfa_ezekiel' and e['kind'] == 'seq' and e['id'] != '-1':
            events.append((t, e['id'].replace('npc_ezekiel_', '')))
        if nm == 'tfa_ezekiel' and e['kind'] == 'say':
            events.append((t, 'say ' + e['raw'][5:]))
        if nm == 'tfa_ezekiel_sword' and e['kind'] == 'add':
            swords[w] = dict(add=t, at=e['at'])
        if nm == 'tfa_ezekiel_sword' and e['kind'] == 'move' and w in swords:
            swords[w]['fly'] = t; swords[w]['to'] = e['at']
        if nm == 'tfa_ezekiel_sword' and e['kind'] == 'hit' and w in swords:
            swords[w].setdefault('hits', []).append((t, e.get('type'), e.get('value')))
    hist[t] = (bpos, dict(ppos), hp)
def at(t):
    ks = [k for k in hist if k <= t]
    return hist[max(ks)] if ks else (None, {}, None)
groups = collections.defaultdict(list)
for w, s in swords.items(): groups[s['add']].append(s)
for S in sorted(groups):
    b, pp, h = at(S)
    print(f"== S={S} boss sw {b[:2] if b else b} hp={h}")
    print("   raiders@S:", {k[3:] if k.startswith('pl:') else k: v[:2] for k, v in pp.items() if v and abs(v[0]-4063) < 12 and abs(v[1]-5535) < 12})
    for s in sorted(groups[S], key=lambda s: (s.get('fly', 0), s['at'])):
        f = s.get('fly')
        bf = at(f - 1)[0] if f else None
        lane = ('col', s['at'][0]) if f and s['to'][0] == s['at'][0] else ('row', s['at'][1])
        hitb = bf and ((lane[0] == 'col' and bf[0] == lane[1]) or (lane[0] == 'row' and bf[1] == lane[1]))
        print(f"   sword {s['at'][:2]} -> {s.get('to', (None,))[:2]} fly {f} (S+{f - S if f else '-'}) lane {lane} boss@fly-1 {bf[:2] if bf else bf} {'HITS BOSS' if hitb else ''} hits={s.get('hits')}")
    for t, x in events:
        if S - 1 <= t <= S + 30: print(f"   [{t}] S+{t - S} {x}  hp={at(t)[2]}")
