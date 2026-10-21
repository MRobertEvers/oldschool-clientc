"""Every Bork gong: the target's distance to his 4x4 at T-1/T, and each beat's
reach (max Chebyshev distance of its dust tiles from his 4x4) at T and T+2."""
import sys, re, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
def bd(sw, c):
    dx = max(0, sw[0]-c[0], c[0]-(sw[0]+3)); dz = max(0, sw[1]-c[1], c[1]-(sw[1]+3)); return max(dx, dz)
for spec in sys.argv[1:]:
    p, a, b = spec.rsplit(':', 2); a = int(a); b = int(b)
    ev = [e for e in load(p) if a-5 <= e['tick'] <= b]
    by = collections.defaultdict(list)
    for e in ev: by[e['tick']].append(e)
    names = {}; bpos = None; pl = {}; target = None; hist = {}; plhist = {}
    gongs = []; dust = collections.defaultdict(list)
    for t in sorted(by):
        for e in by[t]:
            w = e['who']
            if 'name' in e and w.startswith('npc:'): names.setdefault(w, e['name'])
            nm = names.get(w, '')
            if nm == 'tfa_bork' and e['kind'] in ('add', 'move'): bpos = e['at']
            if nm == 'tfa_bork' and e['kind'] == 'face':
                m = re.search(r'name=([^,]+),', e['raw'])
                if m: target = m.group(1)
            if w.startswith('pl:') and e['kind'] in ('tile', 'move'): pl[w[3:]] = e['at']
            if nm == 'tfa_bork' and e['kind'] == 'seq' and 'gong' in e['id']: gongs.append(t)
            if e['kind'] == 'map_anim' and e['id'] == 'tfa_wave_dust_short': dust[t].append(e['at'])
        hist[t] = bpos; plhist[t] = dict(pl)
    for g in gongs:
        out = []
        for k in (0, 2):
            tiles = dust.get(g+k, [])
            sw = hist.get(g+k) or hist.get(g)
            reach = max((bd(sw, c) for c in tiles), default=0)
            out.append(f"{len(tiles):3d}t r{reach}")
        tp = plhist.get(g, {}).get(target) if target else None
        tp1 = plhist.get(g-1, {}).get(target) if target else None
        d = bd(hist[g], tp) if tp else None
        d1 = bd(hist[g-1], tp1) if tp1 and hist.get(g-1) else None
        prev = gongs[gongs.index(g)-1] if gongs.index(g) > 0 else None
        print(f"{p[-8:-4]} gong {g} gap {g-prev if prev else '-':>3}  target {str(target)[:8]:8s} d(T-1)={d1} d(T)={d}  beat1 {out[0]}  beat2 {out[1]}")
