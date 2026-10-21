import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
path, boss, ticks = sys.argv[1], sys.argv[2], [int(x) for x in sys.argv[3].split(',')]
anim = sys.argv[4] if len(sys.argv) > 4 else 'tfa_wave_dust_short'
ev = load(path)
names = {}; bpos = None; pos_at = {}
grids = collections.defaultdict(dict)
pl = {}
for e in ev:
    w = e['who']
    if 'name' in e and w.startswith('npc:'): names.setdefault(w, e['name'])
    if names.get(w) == boss and e['kind'] in ('add', 'move'): bpos = e['at']
    if w.startswith('pl:') and e['kind'] in ('tile', 'move'): pl[w] = e['at']
    if e['kind'] == 'map_anim' and e['id'] == anim and e['tick'] in ticks:
        pos_at[e['tick']] = bpos
        grids[e['tick']][(e['at'][0]-bpos[0], e['at'][1]-bpos[1])] = e['delay']
    if e['tick'] in ticks and w.startswith('pl:') and e['kind'] == 'hit':
        grids[e['tick']][('hit', w[3:8], (pl[w][0]-bpos[0], pl[w][1]-bpos[1]))] = e['value']
for t in ticks:
    g = grids[t]
    print(f"[{t}] boss sw {pos_at.get(t)} hits {[ (k[1],k[2],v) for k,v in g.items() if k[0]=='hit']}")
    tiles = [k for k in g if k[0] != 'hit']
    if not tiles: continue
    xs = [x for x, z in tiles]; zs = [z for x, z in tiles]
    for z in range(max(zs), min(zs)-1, -1):
        row = ''
        for x in range(min(xs), max(xs)+1):
            if (x, z) in g:
                d = g[(x, z)]
                row += (str(d) if d else '0').rjust(3)
            elif 0 <= x <= 3 and 0 <= z <= 3:
                row += '  B'
            else:
                row += '  .'
        print(f"  {z:+3d} {row}")
