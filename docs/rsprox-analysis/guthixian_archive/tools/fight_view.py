"""One fight, per tick: my tile, the guardian's tile, distance, and every event of note.
    python3 fight_view.py SESSION T0 T1"""
import sys
from tfa_events import load
A = '/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
s, t0, t1 = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
ev = load(A + f'idx{s}.txt')
me = None; npcs = {}; names = {}
rows = {}
def put(t, txt): rows.setdefault(t, []).append(txt)
SKIP_VARBIT = ('date_', 'clock', 'hpbar_hud_hp', 'busy', 'prayer_allactive')
for e in ev:
    t = e['tick']
    w = e['who']; k = e['kind']
    if w == 'me' and k in ('tile', 'move'): me = e['at']
    if w.startswith('npc'):
        if e.get('name'): names[w] = e['name']
        if k in ('add', 'move'): npcs[w] = e['at']
        if k == 'transform': names[w] = e['id']
    if not (t0 <= t <= t1): continue
    n = names.get(w, '')
    short = n.replace('tfa_guardian', 'G').replace('archive_guardian_tentacle', 'tent').replace('npc_archive_guardian_', '').replace('npc_guardian_archive_', '')
    if w == 'me':
        if k == 'hit': put(t, f'ME hit {e["value"]} ({e["type"]})')
        elif k == 'seq' and e['id'] not in ('snakeboss_blowpipe_attack', 'human_bow', 'slayer_abyssal_whip_attack'): put(t, f'ME seq {e["id"]}')
        elif k == 'spot' and 'faerdhinen' not in e['id']: put(t, f'ME spot {e["id"]}')
    elif w.startswith('npc') and ('guardian' in n or 'tentacle' in n or 'battery' in n):
        if k == 'seq': put(t, f'{short}#{w[4:]} seq {e["id"].replace("npc_archive_guardian_","").replace("npc_guardian_archive_","")}')
        elif k == 'add': put(t, f'{short}#{w[4:]} ADD {e["at"][:2]} {e.get("spawnangle")}')
        elif k == 'del': put(t, f'{short}#{w[4:]} DEL')
        elif k == 'move': put(t, f'{short}#{w[4:]} {e["mode"]} {e["frm"][:2]}->{e["at"][:2]}')
        elif k == 'transform': put(t, f'{short}#{w[4:]} -> {e["id"]}')
        elif k == 'hit' and e['value'] >= 100: put(t, f'{short}#{w[4:]} HIT {e["value"]} ({e["type"]})')
        elif k == 'face' and 'tent' in short: pass
        elif k == 'exactmove': put(t, f'{short} exactmove {e["raw"]}')
        elif k == 'spot' and 'ranged' not in e['id'] and 'thrall' not in e['id'] and 'xbows' not in e['id'] and 'sanguin' not in e['id'] and e['id'] != '-1': put(t, f'{short} spot {e["id"].replace("vfx_guardian_","")}')
    elif w == 'zone':
        if k == 'loc_add' and 'invisible' not in e['id']: put(t, f'LOC {e["id"].replace("tfa_guardian_","")} {e["at"][:2]}')
        elif k == 'loc_add' and 'invisible' in e['id']: put(t, f'inv {e["id"][15:]} {e["at"][:2]}')
        elif k == 'map_anim': put(t, f'MAPANIM {e["id"]} {e["at"][:2]} d={e["delay"]}')
        elif k == 'message' and e['type'] != 'spam': put(t, f'MSG {e["text"]}')
        elif k == 'sound_area' and e['id'] in ('12218', '12175', '12728'): put(t, f'SND {e["id"]} {e["at"][:2]} r={e["range"]} d={e["delay"]} size={e["size"]}')
        elif k == 'varbit' and not e['id'].startswith(SKIP_VARBIT): put(t, f'VARBIT {e["id"]}={e["new"]}')
        elif k == 'cam_shake': put(t, f'SHAKE')
        elif k == 'proj' and e['id'] == 'vfx_guardian_ranged_projanim01': put(t, 'G proj')
    if t in rows or True:
        pass
# position columns
pos = {}
me = None; g = None
for e in ev:
    t = e['tick']
    if e['who'] == 'me' and e['kind'] in ('tile', 'move'): me = e['at']
    if e['who'].startswith('npc') and e['kind'] in ('add', 'move') and 'tfa_guardian' in (e.get('name') or '') and 'battery' not in e.get('name', ''): g = e['at']
    pos[t] = (me, g)
for t in range(t0, t1 + 1):
    if t not in rows: continue
    m, gg = pos.get(t, (None, None))
    d = ''
    if m and gg:
        # guardian is size 5: distance from player to the nearest tile of its 5x5 square
        dx = max(gg[0] - m[0], 0, m[0] - (gg[0] + 4)); dz = max(gg[1] - m[1], 0, m[1] - (gg[1] + 4))
        d = max(dx, dz)
    print(f'{t:5} me={m[:2] if m else "-"} G={gg[:2] if gg else "-"} d={d}  | ' + ' ; '.join(rows[t]))
