"""Per-tick compact view of a boss room with coordinates relative to the boss.

    python3 boss_view.py <all-players txt> <boss name regex> <from> <to> [--skip regex]

Each tick: B=boss sw tile (abs) + its seq/spot/say/hits taken total; players
(abs tile, hits taken); other npcs' adds/seqs/moves (rel to boss); map_anims
grouped by (id, delay) with tile list rel to boss sw; sound_area; loc adds
(non-invisible); projectiles (src->dst rel).
"""
import sys, re, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load

path, bossre, t0, t1 = sys.argv[1], re.compile(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
skip = re.compile(r'thrall|_pet|pet_|leagues|resurrect|glaive|bloodhound|halberd|dragon_arrow|zcb|crimson|warhammer|dharok|scythe|blood_sac|abyssal|ancient|tumeken|venator|ice_barrage|blood_barrage|smoke|shadow|sang|twisted|bow|dart|blowpipe|xbow|ruby|dragon|' + (sys.argv[6] if len(sys.argv) > 6 and sys.argv[5] == '--skip' else 'zzzz'))
ev = [e for e in load(path) if t0 - 50 <= e['tick'] <= t1]
names = {}
boss = None
bpos = None
byt = collections.defaultdict(list)
for e in ev:
    byt[e['tick']].append(e)
pl_pos = {}
def rel(c):
    if not c or not bpos:
        return str(c)
    return f"{c[0]-bpos[0]:+d},{c[1]-bpos[1]:+d}"
for t in sorted(byt):
    out = []
    mapan = collections.defaultdict(list)
    for e in byt[t]:
        w = e['who']
        if w.startswith('npc:') and 'name' in e:
            names[w] = e['name']
        nm = names.get(w, '')
        if e['kind'] == 'transform':
            names[w] = e['id']
        isboss = w.startswith('npc:') and bossre.search(nm or '')
        if isboss and e['kind'] in ('add', 'move'):
            boss = w; bpos = e['at']
        if e['kind'] in ('tile', 'move') and w.startswith('pl:'):
            pl_pos[w[3:]] = e['at']
    if t < t0:
        continue
    for e in byt[t]:
        w = e['who']; nm = names.get(w, w); k = e['kind']
        if skip.search(str(nm)) or skip.search(str(e.get('id', ''))):
            continue
        isboss = w == boss
        if k == 'varbit' and e['id'] == 'hpbar_hud_hp':
            out.append(f"hp {e['old']}->{e['new']}")
        if isboss and k == 'hit':
            out.append(f"B hit {e['type']}:{e['value']}")
        if isboss:
            if k == 'seq': out.append(f"B seq {e['id']}")
            elif k == 'spot': out.append(f"B spot {e['id']} s{e['slot']} d{e['delay']} h{e['height']}")
            elif k == 'say': out.append(f"B say {e['raw']}")
            elif k == 'move': out.append(f"B {e['mode']} ->{e['at'][0]},{e['at'][1]}")
            elif k == 'add': out.append(f"B add {e['at']} {e['spawnangle']}")
            elif k == 'face': out.append(f"B face {e['raw'][:60]}")
            elif k in ('ops', 'transform', 'level', 'exactmove', 'tinting'): out.append(f"B {k} {e.get('opflags') or e.get('id') or e.get('level') or e.get('raw')}")
            elif k == 'del': out.append("B del")
        elif w.startswith('npc:'):
            if k == 'add': out.append(f"+{nm}@{rel(e['at'])} {e['spawnangle']}")
            elif k == 'del': out.append(f"-{nm}@{rel(e['at'])}")
            elif k == 'seq': out.append(f"{nm} seq {e['id']}")
            elif k == 'spot': out.append(f"{nm} spot {e['id']} s{e['slot']} d{e['delay']}")
            elif k == 'move': out.append(f"{nm} {e['mode']} {rel(e['frm'])}->{rel(e['at'])}")
            elif k == 'say': out.append(f"{nm} say {e['raw']}")
            elif k == 'hit': out.append(f"{nm} hit {e['type']}:{e['value']}")
            elif k in ('transform', 'ops'): out.append(f"{nm} {k} {e.get('id') or e.get('opflags')}")
        elif w.startswith('pl:'):
            p = w[3:]
            if k == 'hit' and e['type'] not in ('13', None) or (k == 'hit' and e['value']):
                out.append(f"{p}@{rel(pl_pos.get(p))} HIT {e['type']}:{e['value']}")
            elif k == 'spot' and not skip.search(str(e['id'])) and e['id'] != '-1':
                out.append(f"{p} spot {e['id']}")
            elif k == 'seq' and re.search(r'tfa|ezekiel|bork|jelly|xarth|ocellus|stun|knock|human_death|fall|strafe', str(e['id'])):
                out.append(f"{p} seq {e['id']}")
            elif k == 'exactmove':
                out.append(f"{p} exactmove {e['raw'][:90]}")
        elif k == 'map_anim':
            mapan[(e['id'], e['delay'])].append(rel(e['at']))
        elif k == 'sound_area':
            out.append(f"snd {e['id']} d{e['delay']} r{e['range']} @{rel(e['at'])}")
        elif k == 'loc_add' and 'invisible' not in str(e['id']):
            out.append(f"loc {e['id']} @{rel(e['at'])} r{e['rot']} s{e['shape']}")
        elif k == 'loc_del':
            out.append(f"locdel @{rel(e['at'])}")
        elif k == 'proj':
            out.append(f"proj {e['id']} {e['starttime']}/{e['endtime']} a{e['angle']} p{e['progress']} h{e['startheight']}/{e['endheight']}")
        elif k == 'message':
            out.append(f"MSG {e['text'][:100]}")
        elif k == 'loc_anim':
            out.append(f"locanim {e['id']} @{rel(e['at'])}")
    for (i, d), tiles in mapan.items():
        out.append(f"anim {i} d{d} x{len(tiles)} [{' '.join(tiles[:40])}]")
    if out:
        ppos = ' '.join(f"{p[:4]}={rel(c)}" for p, c in pl_pos.items() if bpos and c and c[2] == bpos[2] and abs(c[0]-bpos[0]) < 25 and abs(c[1]-bpos[1]) < 25)
        print(f"[{t}] B@{bpos[0] if bpos else ''},{bpos[1] if bpos else ''} | {ppos}")
        for o in out:
            print("   ", o)
