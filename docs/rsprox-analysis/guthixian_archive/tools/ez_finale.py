import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
path, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ev = [e for e in load(path) if a <= e['tick'] <= b]
names = {}; vfxpos = {}; bpos = None; plpos = {}
volleys = collections.defaultdict(lambda: {'tele': [], 'imp': collections.defaultdict(dict)})
hits = []
for e in ev:
    w = e['who']
    if 'name' in e and w.startswith('npc:'): names.setdefault(w, e['name'])
    nm = names.get(w, '')
    if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move'): bpos = e['at']
    if nm == 'tfa_ezekiel_finale_vfx' and e['kind'] == 'add': vfxpos[w] = (e['at'][0]-bpos[0], e['at'][1]-bpos[1])
    if nm == 'tfa_ezekiel_finale_vfx' and e['kind'] == 'spot' and 'telegraph' in str(e['id']):
        volleys[e['tick']]['tele'].append((vfxpos[w], int(e['delay'] or 0), e['id'][-2:]))
    if e['kind'] == 'map_anim' and 'finale_impact' in str(e['id']):
        d = int(e['delay']); r = (e['at'][0]-bpos[0], e['at'][1]-bpos[1])
        volleys[e['tick']]['imp'][(d-124)//30][r] = ('M' if 'medium' in e['id'] else 's') + str(d)
    if w.startswith('pl:') and e['kind'] in ('tile', 'move'): plpos[w] = e['at']
    if w.startswith('pl:') and e['kind'] == 'hit' and e['type'] in ('16', '17'):
        p = plpos.get(w); hits.append((e['tick'], w[3:8], (p[0]-bpos[0], p[1]-bpos[1]) if p else None, e['value']))
for t, v in sorted(volleys.items()):
    print(f"[{t}] vfx at {sorted(set(vfxpos.values()))}")
    for strike in range(3):
        lit = sorted(p for p, d, i in v['tele'] if d == strike*30)
        grid = v['imp'].get(strike, {})
        print(f"  strike {strike} (d{strike*30}) lit {lit}")
        for z in (5, 4, 3):
            print('     ', ' '.join(grid.get((x, z), '  .  ').ljust(5) for x in (0, 1, 2)))
print('hits', [h for h in hits])
