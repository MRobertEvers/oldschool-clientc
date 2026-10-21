import sys, re, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load
path, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ev = [e for e in load(path) if t0 <= e['tick'] <= t1]
names = {}; boss = None; bpos = None
hp = None
for e in ev:
    w = e['who']
    if 'name' in e and w.startswith('npc:'): names.setdefault(w, e['name'])
    nm = names.get(w, '')
    if nm == 'tfa_ezekiel' and e['kind'] in ('add', 'move'): bpos = e['at']
    if e['kind'] == 'varbit' and e['id'] == 'hpbar_hud_hp':
        hp = e['new']
    if nm.startswith('tfa_ezekiel_sword') and nm != 'tfa_ezekiel_sword_noop':
        k = e['kind']
        rel = lambda c: f"{c[0]-bpos[0]:+d},{c[1]-bpos[1]:+d}" if c and bpos else c
        if k == 'add': print(f"[{e['tick']}] {w} add {rel(e['at'])} (B {bpos[:2]})")
        elif k == 'headbar': print(f"[{e['tick']}] {w} headbar {e['id']} fill={e['fill']} rm={e['removed']}")
        elif k == 'hit': print(f"[{e['tick']}] {w} hit {e['type']}:{e['value']}")
        elif k == 'move': print(f"[{e['tick']}] {w} {e['mode']} {rel(e['frm'])}->{rel(e['at'])}")
        elif k in ('seq', 'spot'): print(f"[{e['tick']}] {w} {k} {e['id']}")
        elif k == 'del': print(f"[{e['tick']}] {w} del")
    if nm == 'tfa_ezekiel':
        if e['kind'] == 'seq' and 'weaken' in e['id'] or e['kind'] == 'spot' and 'heal' in str(e['id']) or e['kind'] == 'say':
            print(f"[{e['tick']}] BOSS {e['kind']} {e.get('id') or e.get('raw')} hp={hp}")
