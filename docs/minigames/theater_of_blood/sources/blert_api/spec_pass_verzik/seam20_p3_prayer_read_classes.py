#!/usr/bin/env python3
"""When does Verzik P3's auto read the protection prayer? Blert stage-15 streams.
PLAYER_UPDATE (type 4) source 0 = the recorder: prayerSet bit 16 Magic, 17 Missiles; hitpoints current<<16.
NPC_ATTACK (type 10) codes in P3 (enum = registry index + 1, with TOB_VERZIK_P3_AUTO at 18):
  19 P3_MELEE, 20 P3_RANGE, 21 P3_MAGE. Verified below by which prayer bit the hit size follows."""
import glob, json, collections, sys
D = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_verzik/'
MAG, MIS = 1 << 16, 1 << 17
CODES = {20: ('RANGE', MIS), 21: ('MAGE', MAG)}
cls = collections.defaultdict(list); total = collections.Counter()
for f in sorted(glob.glob(D + '1[12]_*.json')):   # Normal (11) and Hard (12) only
    ev = json.load(open(f)); tag = f.split('/')[-1][:7]
    prim = None; pr = {}; hp = {}
    for e in ev:
        if e['type'] == 4 and e['player'].get('source') == 0:
            prim = e['player']['name']; t = e['tick']
            pr[t] = e['player'].get('prayerSet', 0)
            h = e['player'].get('hitpoints'); hp[t] = (h >> 16) if isinstance(h, int) else None
    for e in ev:
        if e['type'] != 10 or e['npcAttack']['attack'] not in CODES or e['npcAttack'].get('target') != prim:
            continue
        name, bit = CODES[e['npcAttack']['attack']]
        T = e['tick']; total[name] += 1
        p = [bool(pr.get(T + o, 0) & bit) for o in range(-1, 8)]     # offsets -1..7
        h = [hp.get(T + o) for o in range(-1, 8)]
        drops = [(o - 1, h[o] - h[o + 1]) for o in range(0, 8) if h[o] is not None and h[o + 1] is not None and h[o + 1] < h[o]]
        line = '%s %s T%d pray(T-1..T+7):%s drops(off,hp):%s' % (tag, name, T, ''.join('P' if x else '-' for x in p), drops)
        onT = p[1]; after = p[2:5]   # T+1..T+3
        if not onT and any(after): cls['A ' + name + ' OFF at throw, ON by T+3'].append((line, drops))
        elif onT and not all(after): cls['B ' + name + ' ON at throw, OFF by T+3'].append((line, drops))
        elif not any(p[1:5]): cls['C ' + name + ' OFF throughout'].append((line, drops))
        elif all(p[1:5]): cls['D ' + name + ' ON throughout'].append((line, drops))
print('P3 autos aimed at the recorder:', dict(total))
for k in sorted(cls):
    rows = cls[k]; mx = max([d for _, ds in rows for _, d in ds] or [0])
    big = sum(1 for _, ds in rows if any(d > 16 for _, d in ds))
    none = sum(1 for _, ds in rows if not ds)
    print('== %s: %d; largest drop %d; rows with a drop >16: %d; rows with no drop: %d' % (k, len(rows), mx, big, none))
    for l, _ in rows[:int(sys.argv[1]) if len(sys.argv) > 1 else 6]: print('  ', l)
