#!/usr/bin/env python3
"""Ancestral Glyph (npc 7707) rows from the wave-69 event streams of the cached Blert runs ->
ZUK_SHIELD_ANALYSIS.txt. Complements zuk_analysis.py (dwell, period, extremes, hp). OBSERVED rows only
(blert/PROVENANCE.md). Tick 0 = the `Wave: 69` chat tick. Types: 4 player, 7 spawn, 8 update, 10 npc attack.
Run: python3 zuk_shield_analysis.py > ZUK_SHIELD_ANALYSIS.txt (input build/corpus_tmp/blert_api, not in git)."""
import json, glob, os, collections
D = os.path.dirname(os.path.abspath(__file__)) + '/'
CACHE = D + '../../../../../build/corpus_tmp/blert_api/'
C = collections.Counter
def dist(c): return ' '.join('%s:%d' % (k, v) for k, v in sorted(c.items(), key=lambda kv: str(kv[0])))
runs = []
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and d.get('waves', {}).get('69'): runs.append(d['waves']['69'])
print('runs with wave 69 streams: %d' % len(runs))
shield_y = C(); first_x = C(); first_six = C(); player_start = C(); hp_lost = []; basehp = C()
off = {'moving west': C(), 'moving east': C(), 'at west end 2257': C(), 'at east end 2283': C()}; player_y = C()
for ev in runs:
    sh = [e for e in ev if e['type'] == 8 and e['npc']['id'] == 7707]
    pos = {e['tick']: e['xCoord'] for e in sh}
    for e in sh: shield_y[e['yCoord']] += 1
    basehp[max(e['npc']['hitpoints'] & 0xffff for e in sh)] += 1
    hp_lost.append(600 - min(e['npc']['hitpoints'] >> 16 for e in sh))
    za = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['id'] == 7706)
    first_x[pos.get(za[0])] += 1
    first_six[tuple(pos.get(t) for t in za[:6])] += 1
    ply = {e['tick']: (e['xCoord'], e['yCoord']) for e in ev if e['type'] == 4}
    p0 = min(ply); player_start[(p0,) + ply[p0]] += 1
    for t in za:
        if t in pos and t in ply and t - 1 in pos:
            x = pos[t]; o = ply[t][0] - x; player_y[ply[t][1]] += 1
            if x == 2257: off['at west end 2257'][o] += 1
            elif x == 2283: off['at east end 2283'][o] += 1
            elif x < pos[t - 1]: off['moving west'][o] += 1
            elif x > pos[t - 1]: off['moving east'][o] += 1
def p(t, v): print('%-52s %s' % (t, v))
p('shield y over every update', dist(shield_y))
p('shield hp at base', dist(basehp))
p('shield hp lost, max over the run (per run)', sorted(hp_lost))
p('shield x at the first Zuk attack', dist(first_x))
p('shield x at the first six Zuk attacks', dist(first_six))
p('player first tick, x, y', dist(player_start))
for k, v in off.items(): p('player.x - shield.x at a Zuk attack, shield ' + k, '%d: %s' % (sum(v.values()), dist(v)))
p('player y at a Zuk attack', dist(player_y))
