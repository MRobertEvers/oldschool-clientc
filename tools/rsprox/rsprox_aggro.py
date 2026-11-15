# Aggression evidence from an RSProx transcript: for every npc instance that
# turns to face the recorder, was it provoked (recorder op'd it first) or not,
# how far away was it, what level was the recorder, did a fight follow.
#
#   python3 tools/rsprox/rsprox_aggro.py <area> <transcript>...
#   area: lumbridge | stronghold | swampcaves | any
#
# EV rows: an npc's first [facing] onto the recorder, with the Chebyshev
# distance from the npc's SW tile and whether the recorder had op'd it in the
# last 50 ticks. PASS rows: an npc the recorder came within 6 tiles of that
# never faced them. See docs/rsprox-analysis/aggression/README.md.
import re, sys, collections, math
NPC = re.compile(r'npc=\(index=(\d+), id=([^,]+?) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
PL = re.compile(r'player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
OP = re.compile(r'\[op(npc\d|npct)[^\]]*\] npc=\(index=(\d+)')
STAT = re.compile(r'\[update_stat[^\]]*\] stat=(\w+), level=(\d+), experience=([\d,]+)')
XP = [0]
p = 0
for lvl in range(1, 100):
    p += int(lvl + 300 * 2 ** (lvl / 7.0))
    XP.append(p // 4)
def base(xp):
    l = 1
    while l < 99 and XP[l] <= xp: l += 1
    return l
def cb(s):
    g = lambda k: s.get(k, 1 if k != 'hitpoints' else 10)
    b = 0.25 * (g('defence') + g('hitpoints') + g('prayer') // 2)
    return int(b + max(0.325 * (g('attack') + g('strength')), 0.325 * (g('ranged') * 3 // 2), 0.325 * (g('magic') * 3 // 2)))
def run(path, area):
    local = None; tick = 0; me = None; stats = {}
    npc_pos = {}; npc_type = {}; provoked = {}; faced = {}; events = []; near = collections.defaultdict(lambda: [99, None, None])
    cur_npc = None; in_player = None
    fight_until = {}
    for line in open(path, errors='replace'):
        if local is None:
            m = re.match(r'local player index: (\d+)', line)
            if m: local = int(m.group(1))
            continue
        m = re.match(r'^\[(\d+)\]', line)
        if m: tick = int(m.group(1)); cur_npc = None; continue
        s = STAT.search(line)
        if s:
            xp = int(s.group(3).replace(',', ''))
            if xp > 0 or s.group(1) not in stats: stats[s.group(1)] = base(xp) if xp > 0 else (10 if s.group(1) == 'hitpoints' else 1)
            continue
        o = OP.search(line)
        if o and line.startswith('    ['):
            provoked[int(o.group(2))] = tick; continue
        n = NPC.search(line)
        if n:
            idx = int(n.group(1)); npc_type[idx] = n.group(2)
            x, z, l = (int(n.group(7)), int(n.group(8)), int(n.group(9))) if n.group(7) else (int(n.group(4)), int(n.group(5)), int(n.group(6)))
            npc_pos[idx] = (x, z, l); cur_npc = idx
            if me and area(x, z) and me[2] == l:
                d = max(abs(x - me[0]), abs(z - me[1]))
                if d < near[idx][0]: near[idx] = [d, tick, cb(stats)]
            continue
        pm = PL.search(line)
        if pm and int(pm.group(1)) == local and ('[facing]' not in line):
            if pm.group(5): me = (int(pm.group(5)), int(pm.group(6)), int(pm.group(7)))
            else: me = (int(pm.group(2)), int(pm.group(3)), int(pm.group(4)))
            cur_npc = None
            continue
        if '[facing] player=' in line and cur_npc is not None:
            fm = PL.search(line)
            if fm and int(fm.group(1)) == local and cur_npc not in faced:
                x, z, l = npc_pos[cur_npc]
                if me and area(x, z):
                    faced[cur_npc] = tick
                    pv = provoked.get(cur_npc)
                    events.append(dict(tick=tick, idx=cur_npc, type=npc_type[cur_npc], dist=max(abs(x - me[0]), abs(z - me[1])),
                                       provoked=(pv is not None and pv <= tick and tick - pv < 50), cb=cb(stats), me=me))
    return events, near, npc_type, faced
if __name__ == '__main__':
    areas = {'lumbridge': lambda x, z: 3136 <= x < 3328 and 3136 <= z < 3328,
             'stronghold': lambda x, z: 1856 <= x < 2368 and 5184 <= z < 5376,
             'swampcaves': lambda x, z: 3136 <= x < 3264 and 9536 <= z < 9600, 'any': lambda x, z: True}
    area = areas[sys.argv[1]]
    for path in sys.argv[2:]:
        ev, near, types, faced = run(path, area)
        for e in ev:
            print('EV', path.split('/')[-1], e['type'], 'dist', e['dist'], 'provoked' if e['provoked'] else 'UNPROVOKED', 'cb', e['cb'], 't', e['tick'])
        for idx, (d, t, c) in near.items():
            if idx not in faced and d <= 6:
                print('PASS', path.split('/')[-1], types[idx], 'mindist', d, 'cb', c, 't', t)
