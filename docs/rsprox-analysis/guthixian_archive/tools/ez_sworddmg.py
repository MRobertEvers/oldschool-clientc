"""Ezekiel: the splat a charged sword leaves on him, against the swords' charge.

For every flight through his south-west tile: the charge each sword through him had
taken (type-23 splats, counted before / on the flight tick), and his splats the tick
after, with the raiders' attack seqs on those ticks (a raider's hit can share it).

    python3 ez_sworddmg.py <all-players txt>...
"""
import sys, collections, re
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
IDLE = re.compile(r'walk|run|idle|defend|block|death|roll|strafe|eat|drink|ezekiel_magic_attack|^-1$|ready')
for path in sys.argv[1:]:
    ev = load(path); names = {}
    for e in ev:
        if 'name' in e and e['who'].startswith('npc:'): names.setdefault(e['who'], e['name'])
    byt = collections.defaultdict(list)
    for e in ev: byt[e['tick']].append(e)
    bpos = None; bpos_at = {}; hpmax = None
    sw = {}; bosshits = collections.defaultdict(list); atk = collections.defaultdict(list)
    for t in sorted(byt):
        for e in byt[t]:
            w = e['who']; nm = names.get(w, '')
            if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move', 'tile'): bpos = e['at']
            if nm == 'tfa_ezekiel' and e['kind'] == 'hit': bosshits[t].append((e.get('type'), e.get('value')))
            if e['kind'] == 'varbit' and e.get('id') == 'hpbar_hud_basehp': hpmax = e['new']
            if nm == 'tfa_ezekiel_sword':
                s = sw.setdefault(w, dict(hits=[]))
                if e['kind'] == 'add': s.update(at=e['at'])
                if e['kind'] == 'move': s.update(fly=t, to=e['at'])
                if e['kind'] == 'hit' and str(e.get('type')) == '23': s['hits'].append((t, e.get('value')))
            if w.startswith('pl:') and e['kind'] == 'seq' and not IDLE.search(e['id']): atk[t].append(f"{w[3:9]}:{e['id']}")
        bpos_at[t] = (bpos, hpmax)
    byfly = collections.defaultdict(list)
    for w, s in sw.items():
        f = s.get('fly')
        if not f: continue
        b = bpos_at[f][0]
        col = s['to'][0] == s['at'][0]
        lane = s['at'][0] if col else s['at'][1]
        if (col and b[0] == lane) or (not col and b[1] == lane):
            before = sum(v for t, v in s['hits'] if t < f)
            on = sum(v for t, v in s['hits'] if t == f)
            byfly[f].append((before, on))
    for f in sorted(byfly):
        through = byfly[f]
        nxt = bosshits.get(f + 1, [])
        big = max([v for ty, v in nxt] or [0])
        print(f"{path.split('idx')[-1][:4]} fly {f} max {bpos_at[f][1]} through {[(b, o) for b, o in through]} "
              f"charge<fly {sum(b for b, o in through)} splats@+1 {[v for ty, v in nxt]} raider seqs @0/+1 {atk.get(f, []) + atk.get(f + 1, [])}")
