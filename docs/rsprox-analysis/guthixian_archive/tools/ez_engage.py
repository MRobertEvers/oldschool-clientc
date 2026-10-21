"""Ezekiel, per engage: the raiders around the bar tick, his moves, faces, line, first seqs.

    python3 ez_engage.py <all-players txt>
"""
import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
ev = load(sys.argv[1]); names = {}
byt = collections.defaultdict(list)
for e in ev:
    if 'name' in e and e['who'].startswith('npc:'): names.setdefault(e['who'], e['name'])
    byt[e['tick']].append(e)
bpos = None; ppos = {}; hist = {}
eng = []; seqs = []; faces = []; says = []; moves = []
for t in sorted(byt):
    for e in byt[t]:
        w = e['who']; nm = names.get(w, '')
        if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move', 'tile'): bpos = e['at']
        if nm == 'tfa_ezekiel' and e['kind'] == 'add': eng.append(['add', t])
        if (w.startswith('pl:') or w == 'me') and e['kind'] in ('move', 'tile') and e.get('at'): ppos[w] = e['at']
        if e['kind'] == 'varp' and e.get('id') == 'hpbar_hud_npc' and str(e.get('new')) == '16652': eng.append(['bar', t])
        if nm == 'tfa_ezekiel' and e['kind'] == 'seq' and e['id'] != '-1': seqs.append((t, e['id'][13:]))
        if nm == 'tfa_ezekiel' and e['kind'] == 'face': faces.append((t, e['raw'][:40]))
        if nm == 'tfa_ezekiel' and e['kind'] == 'say': says.append((t, e['raw'][6:30]))
        if nm == 'tfa_ezekiel' and e['kind'] == 'move': moves.append((t, e['mode'], e['frm'][:2], e['at'][:2]))
    hist[t] = (bpos, dict(ppos))
def d(sw, p):
    dx = max(0, sw[0] - p[0], p[0] - sw[0] - 2); dz = max(0, sw[1] - p[1], p[1] - sw[1] - 2); return max(dx, dz)
for kind, t in eng:
    if kind != 'bar': continue
    print('=== engage (bar) at', t)
    for k in range(t - 3, t + 1):
        if k in hist and hist[k][0]:
            b, pp = hist[k]
            print('   ', k, 'boss', b[:2], {n[3:9]: (d(b, p), p[:2]) for n, p in pp.items() if abs(p[0] - 4063) < 14 and abs(p[1] - 5535) < 10})
    print('    moves', [m for m in moves if t - 12 <= m[0] <= t + 8])
    print('    faces', [f for f in faces if t - 2 <= f[0] <= t + 6])
    print('    says', [s for s in says if t - 2 <= s[0] <= t + 6])
    print('    seqs', [s for s in seqs if t <= s[0] <= t + 20])
