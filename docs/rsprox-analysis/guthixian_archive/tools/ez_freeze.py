"""Ezekiel: does he step, by ticks since his special / weaken / heal, the tank out of reach.

    python3 ez_freeze.py <all-players txt>...
"""
import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
tab = collections.defaultdict(collections.Counter)
for path in sys.argv[1:]:
    ev = load(path); names = {}
    byt = collections.defaultdict(list)
    for e in ev:
        if 'name' in e and e['who'].startswith('npc:'): names.setdefault(e['who'], e['name'])
        byt[e['tick']].append(e)
    bpos = None; ppos = {}; tank = None; fight = False; last = None
    for t in sorted(byt):
        prev_b = bpos; prev_p = dict(ppos); moved = False
        for e in byt[t]:
            w = e['who']; nm = names.get(w, '')
            if nm == 'tfa_ezekiel':
                if e['kind'] in ('add', 'tile'): bpos = e['at']
                if e['kind'] == 'move': bpos = e['at']; moved = True
                if e['kind'] == 'face' and 'player=' in e['raw']: tank = e['raw'].split('name=')[1].split(',')[0]
                if e['kind'] == 'seq' and e['id'] != '-1':
                    s = e['id'].replace('npc_ezekiel_', '')
                    if s in ('special01', 'idle_to_weaken01', 'heal01', 'melee01', 'magic01'): last = (s, t)
                    if 'finale' in s or s == 'walk01': fight = False
                if e['kind'] == 'del': fight = False
            if e['kind'] == 'varp' and e.get('id') == 'hpbar_hud_npc': fight = str(e.get('new')) == '16652'
            if w.startswith('pl:') and e['kind'] in ('move', 'tile') and e.get('at'): ppos[w[3:]] = e['at']
        if not fight or not prev_b or tank not in prev_p or not last: continue
        # tank pos after his move this tick
        p = ppos.get(tank); b = prev_b
        rx, rz = p[0] - b[0], p[1] - b[1]
        beside = ((rx in (-1, 3) and 0 <= rz <= 2) or (rz in (-1, 3) and 0 <= rx <= 2))
        if beside: continue
        off = t - last[1]
        if last[0] == 'special01' or off == 0 or last[0] in ('idle_to_weaken01', 'heal01'):
            tab[last[0]][(off, 'moved' if moved else 'stay')] += 1
for k, c in tab.items():
    print(k)
    for off in range(0, 20):
        m, s = c[(off, 'moved')], c[(off, 'stay')]
        if m or s: print(f"   +{off}: moved {m} stay {s}")
