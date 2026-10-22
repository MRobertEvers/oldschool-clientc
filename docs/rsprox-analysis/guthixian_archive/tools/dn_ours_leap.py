"""Our tick log's leaps against the recorded rule (dn_leaprule.py): his sw before and
after each jump02, his faced target's tile at the start of the tick."""
import sys, math, collections
cl = lambda v, lo, hi: max(lo, min(hi, v))
rows = collections.defaultdict(list)
for line in open(sys.argv[1], errors='replace'):
    f = line.rstrip('\n').split('\t')
    if len(f) < 9 or not f[1].isdigit(): continue
    rows[int(f[1])].append(f[2:])
pos = {}; boss = None; sw = None; face = None; spawn = None; ok = 0; n = 0
for t in sorted(rows):
    start = dict(pos); before = sw
    for r in rows[t]:
        if r[0] == 'player_tile': pos[int(r[1])] = (int(r[2]), int(r[3]))
        if r[0] == 'npc_spawn' and r[2] == '16665':
            boss = r[1]; p = int(r[3]); sw = ((p >> 14) & 0x3FFF, p & 0x3FFF); spawn = sw; before = sw
        if r[0] == 'npc_tile' and r[1] == boss: sw = (int(r[2]), int(r[3]))
        if r[0] == 'npc_face' and r[1] == boss and r[3] == '-1': face = int(r[4]) - 32768
    if any(r[0] == 'npc_anim' and r[1] == boss and r[3] == '14780' for r in rows[t]):
        p = start.get(face); n += 1
        need = (p[0] - 2 - before[0], p[1] - 2 - before[1])
        if max(abs(need[0]), abs(need[1])) <= 5: mv = need
        else:
            o = round(math.atan2(need[1], need[0]) / (math.pi / 4)) % 8
            d = [(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)][o]; mv = (5 * d[0], 5 * d[1])
        want = (cl(before[0] + mv[0], spawn[0] - 5, spawn[0] + 5), cl(before[1] + mv[1], spawn[1] - 5, spawn[1] + 5))
        ok += want == sw
        if want != sw: print(f"  [{t}] from {before} to {sw} rule {want} target {p} need {need}")
print(f"leaps on the rule: {ok}/{n}")
