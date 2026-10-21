"""Swoosh trigger: the player's distance (to the guardian's 5x5) on the tick before each
attack slot and each swoosh, and where the swoosh lands relative to the player."""
import collections
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS = {4296: [(355, 840)], 4297: [(90, 265), (345, 645), (810, 1185)]}
def dist(m, g):
    dx = max(g[0] - m[0], 0, m[0] - (g[0] + 4)); dz = max(g[1] - m[1], 0, m[1] - (g[1] + 4))
    return max(dx, dz)
hist = collections.defaultdict(collections.Counter)
for s, wins in FIGHTS.items():
    ev = load(A + f'idx{s}.txt')
    me_at = {}; g_at = {}; me = g = None
    acts = []
    for e in ev:
        t = e['tick']
        if e['who'] == 'me' and e['kind'] in ('tile', 'move'): me = e['at']
        n = e.get('name', '')
        if e['who'].startswith('npc') and 'tfa_guardian' in n and 'battery' not in n:
            if e['kind'] in ('add', 'move'):
                if e['kind'] == 'move': acts.append((t, 'W', g, e['at'], e['frm']))
                g = e['at']
            if e['kind'] == 'seq' and e['id'].endswith(('range_attack01', 'special_attack01', 'melee_attack01')):
                acts.append((t, e['id'].split('_')[3][0].upper(), g, g, None))
        me_at[t] = me; g_at[t] = g
    for (t0, t1) in wins:
        for (t, c, gold, gnew, frm) in acts:
            if not (t0 <= t <= t1): continue
            m = me_at.get(t - 1)
            if not m or not gold: continue
            d = dist(m, gold)
            hist[c][d] += 1
            if c == 'W':
                mnow = me_at.get(t)
                print(f'{s} {t} W  me(t-1)={m[:2]} Gold={gold[:2]} d={d} -> Gnew={gnew[:2]} d_new(t-1)={dist(m, gnew)} d_new(now)={dist(mnow, gnew)}  rel=({gnew[0]-m[0]},{gnew[1]-m[1]})')
for c, h in hist.items():
    print(c, dict(sorted(h.items())))
