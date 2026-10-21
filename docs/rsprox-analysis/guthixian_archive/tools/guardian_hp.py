"""Guardian and battery damage per fight: cumulative damage at each transition."""
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS = {4294: [(540, 1050), (1095, 1495)], 4296: [(355, 840)], 4297: [(90, 265), (345, 645), (810, 1185)]}
for s, wins in FIGHTS.items():
    ev = load(A + f'idx{s}.txt')
    names = {}
    for (t0, t1) in wins:
        print(f'===== {s} [{t0}..{t1}]')
        dmg = {}; fill = {}
        for e in ev:
            w = e['who']
            if w.startswith('npc'):
                if e.get('name'): names.setdefault(w, e['name'])
                if e['kind'] == 'transform': names[w] = e['id']
            if not (t0 <= e['tick'] <= t1) or not w.startswith('npc'): continue
            n = names.get(w, '')
            if 'tfa_guardian' not in n: continue
            k = e['kind']
            g = 'battery' not in n
            key = 'G' if g else w
            if k == 'hit' and e['type'] in ('16', '17', '43'):
                dmg[key] = dmg.get(key, 0) + e['value']
                if e['value'] >= 100: print(f'{e["tick"]:5} {key} big hit {e["value"]} total {dmg[key]} fill {fill.get(key)}')
            elif k == 'headbar' and e.get('fill'):
                fill[key] = e['fill']
            elif k in ('transform', 'seq') and (k == 'transform' or 'enraged' in e['id'] or 'death' in e['id'] or 'despawn' in e['id'] or 'ground' in e['id']):
                print(f'{e["tick"]:5} {key} {k} {e["id"]} dmg={dmg.get(key,0)} fill={fill.get(key)}')
        print('  total damage', dmg)
