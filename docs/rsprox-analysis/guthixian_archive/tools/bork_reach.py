"""Bork's first-beat reach against his target's distance on the tick before.
    python3 bork_reach.py <all-players txt>:<from>:<to> ...
Counts gongs (the stun-tick ones aside: their dust is the cannonball's)."""
import sys, re, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
def bd(sw, c):
    dx = max(0, sw[0]-c[0], c[0]-(sw[0]+3)); dz = max(0, sw[1]-c[1], c[1]-(sw[1]+3)); return max(dx, dz)
tally = collections.Counter(); odd = []
for spec in sys.argv[1:]:
    p, a, b = spec.rsplit(':', 2); a = int(a); b = int(b)
    ev = [e for e in load(p) if a-5 <= e['tick'] <= b]
    by = collections.defaultdict(list)
    for e in ev: by[e['tick']].append(e)
    names = {}; bpos = None; pl = {}; target = None; hist = {}; plh = {}; gongs = []; dust = collections.defaultdict(list); stuns = set(); tg = {}
    for t in sorted(by):
        for e in by[t]:
            w = e['who']
            if 'name' in e and w.startswith('npc:'): names.setdefault(w, e['name'])
            nm = names.get(w, '')
            if nm == 'tfa_bork' and e['kind'] in ('add', 'move'): bpos = e['at']
            if nm == 'tfa_bork' and e['kind'] == 'face' and target is None:
                m = re.search(r'name=([^,]+),', e['raw'])
                if m: target = m.group(1)
            if nm == 'tfa_bork' and e['kind'] == 'say':
                m = re.search(r'Bork smash (.*) now', e['raw'])
                if m: target = m.group(1)
            if w.startswith('pl:') and e['kind'] in ('tile', 'move'): pl[w[3:]] = e['at']
            if nm == 'tfa_bork' and e['kind'] == 'seq' and 'gong' in e['id']: gongs.append(t)
            if nm == 'tfa_bork' and e['kind'] == 'seq' and 'stun' in e['id']: stuns.add(t)
            if e['kind'] == 'map_anim' and e['id'] == 'tfa_wave_dust_short': dust[t].append(e['at'])
        hist[t] = bpos; plh[t] = dict(pl); tg[t] = target
    for g in gongs:
        if any(s in range(g, g+6) for s in stuns): continue
        tp = plh.get(g-1, {}).get(tg[g]); sw = hist.get(g-1)
        if not tp or not sw or not dust.get(g): continue
        d = bd(sw, tp)
        reach = max(bd(sw, c) for c in dust[g])
        if d > 10: odd.append((p[-8:-4], g, d)); continue
        tally[(min(d, 1) if d <= 1 else 'out', reach == max(1, d))] += 1
print(tally, 'skipped (target off the arena):', odd)
