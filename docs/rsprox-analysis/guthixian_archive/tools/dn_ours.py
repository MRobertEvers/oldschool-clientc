"""Our tick log's Dreadnoth room measured the way dn_rules/dn_melee/dn_jtarget/
dn_jswing/dn_seed2 measure the recordings: the boss's step rule, crawl gaps,
swings on step ticks, who a swing hits, switches against J+10 / landings, each
jelly's first target and its swing geometry, seeds per raider tile.
usage: dn_ours.py <ticklog.tsv>"""
import sys, collections
BOSS, JELLY = 16665, 16666
def unpack(p): return ((p >> 14) & 0x3FFF, p & 0x3FFF)
rows = collections.defaultdict(list)
for line in open([a for a in sys.argv[1:] if not a.startswith('-')][0], errors='replace'):
    f = line.rstrip('\n').split('\t')
    if len(f) < 9 or not f[1].isdigit(): continue
    rows[int(f[1])].append((f[2], [int(x) if x.lstrip('-').isdigit() else x for x in f[3:9]]))
ticks = sorted(rows)
pos = {}; prevpos = {}; npcpos = {}; ntype = {}; face = {}; out = collections.Counter()
last_step = {}; last_swing = None; jfirst = {}; jstep = {}; gaps = collections.Counter(); boss_slot = None
J = []; E = []; switches = []
def boxd(sw, p, n):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + n - 1) if v > lo + n - 1 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
sg = lambda v: (v > 0) - (v < 0)
for t in ticks:
    start = dict(pos)
    for kind, v in rows[t]:
        if kind == 'player_tile': pos[v[0]] = (v[1], v[2])
    moved = {}
    for kind, v in rows[t]:
        if kind == 'npc_spawn':
            ntype[v[0]] = v[1]; npcpos[v[0]] = unpack(v[2])
            if v[1] == BOSS: boss_slot = v[0]
        elif kind == 'npc_tile' and v[0] in ntype:
            moved[v[0]] = (npcpos.get(v[0]), (v[1], v[2])); npcpos[v[0]] = (v[1], v[2])
        elif kind == 'npc_face' and v[2] == -1 and v[0] in ntype:
            new = v[3] - 32768 if v[3] >= 32768 else None
            if ntype[v[0]] == BOSS and face.get(v[0]) is not None and new != face.get(v[0]): switches.append(t)
            if ntype[v[0]] == JELLY and v[0] not in jfirst and new is not None and new in start:
                jp = npcpos[v[0]]; ds = {pid: max(abs(p[0]-jp[0]), abs(p[1]-jp[1])) for pid, p in start.items()}
                jfirst[v[0]] = 1; out['jelly first face nearest' if ds[new] == min(ds.values()) else 'jelly first face NOT nearest'] += 1
            face[v[0]] = new
    for slot, (a, b) in moved.items():
        if ntype[slot] == BOSS and a and max(abs(b[0]-a[0]), abs(b[1]-a[1])) == 1:
            tg = face.get(slot); ps = start.get(tg)
            if ps:
                want = (sg(ps[0]-a[0]), sg(ps[1]-a[1])); got = (b[0]-a[0], b[1]-a[1])
                out['boss step = sw rule' if want == got else 'boss step other'] += 1
                if want != got and '-v' in sys.argv: print(f"    [{t}] step {got} sw {a} target {tg} at {ps} (end {pos.get(tg)}) want {want} boxd {boxd(a, ps, 5)} others {[(k, start[k]) for k in start if k != tg]}")
            if slot in last_step: gaps[t - last_step[slot]] += 1
            last_step[slot] = t
        if ntype[slot] == JELLY: jstep[slot] = t
    for kind, v in rows[t]:
        if kind != 'npc_anim': continue
        slot, typ, seq = v[0], v[1], v[2]
        if typ == BOSS and seq == 14777: J.append(t)
        if typ == BOSS and seq == 14780: E.append(t)
        if typ == BOSS and seq == 14779:
            out['boss swing on a step tick' if slot in moved else 'boss swing on a rest tick'] += 1
            sw = npcpos[slot]
            hit = {h[1][0] for h in rows[t] if h[0] == 'hit_player' and h[1][1] == slot}
            for pid, p in start.items():
                d = boxd(sw, pos[pid], 5)
                if d <= 3: out[f'swing: raider at d={d} {"hit" if pid in hit else "not hit"}'] += 1
                if d <= 1 and pid not in hit and '-v' in sys.argv: print(f"    [{t}] swing sw {sw} raider {pid} start {p} end {pos[pid]} hits {[h[1] for h in rows[t] + rows.get(t+1, []) if h[0] == 'hit_player']}")
        if typ == JELLY and seq == 1586:
            tg = face.get(slot); p = start.get(tg); j = npcpos[slot]
            if p:
                dx, dz = abs(p[0]-j[0]), abs(p[1]-j[1])
                out['jelly swing ' + ('under' if dx == dz == 0 else 'side' if dx + dz == 1 else 'corner' if dx == dz == 1 else 'far')] += 1
                if jstep.get(slot) == t: out['jelly swing on its step tick'] += 1
# switches vs events
groups = []
for j in J:
    if groups and j - groups[-1][-1] <= 7: groups[-1].append(j)
    else: groups.append([j])
for s in switches:
    tag = 'other'
    if any(s == g[-1] + 10 for g in groups): tag = 'J+10'
    if any(s - e == 3 for e in E): tag = 'landing (E+3)'
    out['switch at ' + tag] += 1
for k in sorted(out): print(f'  {k}: {out[k]}')
print('  crawl gaps', sorted(gaps.items()))
print('  slam phases', groups, 'leaps', len(E))
