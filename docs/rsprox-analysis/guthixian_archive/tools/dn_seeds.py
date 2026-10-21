"""Per slam: raiders' distinct tiles at the start of J+4, the seeds, and each seed's
tiles (the seed + every jelly_spawning_idle map projectile from it over J+4..J+8)."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
MA = re.compile(r'^ {4}\[map_anim\] id=(\w+) \(\d+\)(?:, delay=(\d+))?.*coord=\((\d+), (\d+), \d+\)')
PROJ = re.compile(r'^ {4}\[map_projanim\w*\] id=jelly_spawning_idle')
SRC = re.compile(r'^ {8}\[source\] coord=\((\d+), (\d+), \d+\)')
TGT = re.compile(r'^ {8}\[target\] coord=\((\d+), (\d+), \d+\)')
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; hist = collections.defaultdict(dict); inb = False; J = []; seeds = collections.defaultdict(set); proj = collections.defaultdict(list); inproj = False; src = None; nraid = {}
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); inb = False; inproj = False; continue
        m = PL.match(line)
        if m: hist[tick][int(m.group(2))] = (int(m.group(4)), int(m.group(5))); inb = False; continue
        m = NPC.match(line)
        if m: inb = m.group(3) == 'tfa_jelly_boss'; continue
        if inb:
            m = SEQ.match(line)
            if m and m.group(1) == 'dreadnoth_jump01': J.append(tick)
            continue
        m = MA.match(line)
        if m and m.group(1) == 'tob_bloat_blood_splat' and int(m.group(2) or 0) == 0: seeds[tick].add((int(m.group(3)), int(m.group(4)))); continue
        if PROJ.match(line): inproj = True; src = None; continue
        if inproj:
            m = SRC.match(line)
            if m: src = (int(m.group(1)), int(m.group(2))); continue
            m = TGT.match(line)
            if m: proj[tick].append((src, (int(m.group(1)), int(m.group(2))))); inproj = False; continue
    groups = []
    for j in J:
        if groups and j - groups[-1][-1] <= 7: groups[-1].append(j)
        else: groups.append([j])
    for g in groups:
        row = []
        for j in g:
            raid = {p for p in hist.get(j + 4, {}).values() if 4180 <= p[0] <= 4202 and 5652 <= p[1] <= 5674}
            sd = seeds.get(j + 4, set()) & (raid | seeds.get(j + 4, set()))
            per = {s: 1 for s in sd}
            for t in range(j + 4, j + 10):
                for s, d in proj.get(t, []):
                    if s in per: per[s] += 1
            row.append(f"J{j}: {len(raid)} tiles, seeds {sorted(per.values(), reverse=True)}")
        print(sess, f"phase of {len(g)}:", ' | '.join(row))
