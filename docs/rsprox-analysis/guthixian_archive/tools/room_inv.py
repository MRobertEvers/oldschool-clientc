"""Inventory of one room window across transcripts (all-players transcripts).

    python3 room_inv.py <txt>:<from>:<to> [...] [--skip name,name]

Prints per npc type: lives, seqs, spotanims, say lines, hits it took/dealt;
projectiles by (id, source, target); map anims, area sounds, loc adds/dels/anims,
messages, varbits. Counts are summed over every window given.
"""
import sys, re, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from tfa_events import load

KIT = re.compile(r'thrall|_pet|pet_|leagues|resurrect|glaive|bloodhound|zebak|olm')
args = [a for a in sys.argv[1:] if not a.startswith('--')]
skip = set()
for a in sys.argv[1:]:
    if a.startswith('--skip='):
        skip |= set(a[7:].split(','))

C = collections.defaultdict(collections.Counter)
lives = collections.defaultdict(list)
for spec in args:
    path, t0, t1 = spec.rsplit(':', 2)
    t0, t1 = int(t0), int(t1)
    ev = [e for e in load(path) if t0 <= e['tick'] <= t1]
    names = {}
    for e in ev:
        w = e['who']
        if w.startswith('npc:') and 'name' in e:
            names[w] = e['name']
        nm = names.get(w, w)
        if nm in skip or KIT.search(str(nm)):
            continue
        k = e['kind']
        if k == 'add':
            lives[nm].append((spec.split('/')[-1][:7], e['tick'], e['at'], e['spawnangle']))
        elif k == 'transform':
            C['transform'][f"{names.get(w)} -> {e['id']}"] += 1
            names[w] = e['id']
        elif k == 'seq' and w.startswith('npc:'):
            C['npc_seq'][f"{nm}: {e['id']} d={e['delay']}"] += 1
        elif k == 'spot' and w.startswith('npc:'):
            C['npc_spot'][f"{nm}: {e['id']} h={e['height']} d={e['delay']} s={e['slot']}"] += 1
        elif k == 'spot' and w.startswith('pl:'):
            if not KIT.search(str(e['id'])):
                C['pl_spot'][f"{e['id']} h={e['height']} d={e['delay']} s={e['slot']}"] += 1
        elif k == 'seq' and w.startswith('pl:'):
            C['pl_seq'][f"{e['id']}"] += 1
        elif k == 'hit' and w.startswith('pl:'):
            C['pl_hit_types'][f"type={e['type']}"] += 1
        elif k == 'hit' and w.startswith('npc:'):
            C['npc_hit'][f"{nm} type={e['type']}"] += 1
        elif k == 'headbar' and w.startswith('npc:'):
            C['npc_headbar'][f"{nm} id={e['id']}"] += 1
        elif k == 'ops':
            C['ops'][f"{nm} {e['opflags']}"] += 1
        elif k == 'level':
            C['level'][f"{nm} {e['level']}"] += 1
        elif k == 'say':
            C['say'][f"{nm}: {e['raw']}"] += 1
        elif k == 'tinting':
            C['tint'][f"{nm}: {e['raw']}"] += 1
        elif k == 'exactmove':
            C['exactmove'][f"{nm}"] += 1
        elif k == 'move' and w.startswith('npc:'):
            C['npc_move'][f"{nm} {e['mode']}"] += 1
        elif k == 'proj':
            C['proj'][f"{e['id']} st={e['starttime']} et={e['endtime']} ang={e['angle']} prog={e['progress']} h={e['startheight']}/{e['endheight']}"] += 1
        elif k == 'map_anim':
            C['map_anim'][f"{e['id']} d={e['delay']} h={e['height']}"] += 1
        elif k == 'sound_area':
            C['sound_area'][f"{e['id']} loops={e['loops']} d={e['delay']} r={e['range']} size={e['size']}"] += 1
        elif k == 'synth':
            C['synth'][f"{e['id']} loops={e['loops']} d={e['delay']}"] += 1
        elif k == 'loc_add':
            if e['id'] not in ('invisible_type8_nonblocking',):
                C['loc_add'][f"{e['id']} shape={e['shape']} rot={e['rot']} ops={e['opflags']}"] += 1
            else:
                C['loc_add']['invisible_type8_nonblocking'] += 1
        elif k == 'loc_del':
            C['loc_del'][f"shape={e['shape']}"] += 1
        elif k == 'loc_anim':
            C['loc_anim'][f"{e['id']}"] += 1
        elif k == 'message':
            t = e['text']
            if not re.search(r'You eat|heals some|You drink|doses? of|Spectating|thrall|resurrect', t):
                C['message'][f"{e['type']}: {t[:150]}"] += 1
        elif k == 'varbit':
            if not re.search(r'date_|tfa_client_p\d|hpbar_hud_hp$|busy|minimap', str(e['id'])):
                C['varbit'][f"{e['id']}"] += 1
        elif k == 'cam_shake':
            C['cam_shake']['x'] += 1
        elif k == 'midi':
            C['midi'][str(e['id'])] += 1
        elif k == 'script':
            C['script'][str(e['id'])] += 1

for nm, ls in sorted(lives.items()):
    print(f"LIVES {nm}: {len(ls)}  e.g. {ls[:3]}")
for sec in ['transform', 'level', 'ops', 'npc_move', 'npc_seq', 'npc_spot', 'say', 'tint', 'exactmove', 'npc_hit', 'npc_headbar', 'proj',
            'map_anim', 'sound_area', 'synth', 'loc_add', 'loc_del', 'loc_anim', 'pl_spot', 'pl_hit_types', 'cam_shake', 'message', 'varbit', 'midi', 'script']:
    if C[sec]:
        print(f"== {sec}")
        for k, v in C[sec].most_common():
            print(f"  {v:5d}  {k}")
