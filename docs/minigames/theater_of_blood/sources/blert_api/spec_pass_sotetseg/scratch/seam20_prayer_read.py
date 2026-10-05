#!/usr/bin/env python3
"""seam20: when does Sotetseg's ordinary ball read the protection prayer?
Blert stage-13 streams (build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw,
21 Normal (m11) and Hard (m12) rooms). The PRIMARY raider (PLAYER_UPDATE source 0) carries
prayerSet (bit 16 = Protect from Magic 65536, 17 = Missiles, 18 = Melee) and hitpoints
(current << 16) on every tick (sources/blert_repo/common__event.ts:429,436). For each
NPC_ATTACK (type 10) attack 8 = TOB_SOTE_BALL aimed at the recorder at tick T, print the
Magic bit and hitpoints from T-1 to T+12 and classify:
  A: Magic OFF on T and ON within T+1..T+4   (a throw read predicts an unprayed hit + 5-tick lock)
  B: Magic ON on T and OFF within T+1..T+4   (a throw read predicts a block)
  C: Magic OFF throughout                     (the flight: the unprayed hit's tick)
"""
import glob, json
D = '/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw/'
MAG = 65536
cls = {'A': [], 'B': [], 'C': []}
total = 0
for f in sorted(glob.glob(D + 'm1*_13.json')):
    ev = json.load(open(f)); tag = f.split('/')[-1][:12]
    prim = None; pr = {}; hp = {}; pos = {}
    for e in ev:
        if e['type'] == 4 and e['player'].get('source') == 0:
            prim = e['player']['name']; t = e['tick']
            pr[t] = e['player'].get('prayerSet', 0)
            h = e['player'].get('hitpoints'); hp[t] = (h >> 16) if isinstance(h, int) else None
            pos[t] = (e['xCoord'], e['yCoord'])
    for e in ev:
        if e['type'] != 10 or e['npcAttack']['attack'] != 8 or e['npcAttack'].get('target') != prim:
            continue
        T = e['tick']; total += 1
        p0 = pos.get(T, (0, 0)); d = max(abs(p0[0] - e['xCoord']), abs(p0[1] - e['yCoord']))
        m = [bool(pr.get(T + o, 0) & MAG) for o in range(-1, 13)]
        h = [hp.get(T + o) for o in range(-1, 13)]
        drops = [(o, h[o] - h[o + 1]) for o in range(0, 13) if h[o] is not None and h[o + 1] is not None and h[o + 1] < h[o]]
        line = '%s T%d d%d mag(T-1..T+12):%s drops(offset,hp):%s' % (tag, T, d, ''.join('M' if x else '-' for x in m), drops)
        if not m[1] and any(m[2:6]): cls['A'].append(line)
        elif m[1] and not all(m[2:6]): cls['B'].append(line)
        elif not any(m[1:]): cls['C'].append(line)
print('balls aimed at the recorder: %d' % total)
for k in 'ABC':
    print('== class %s: %d' % (k, len(cls[k])))
    for l in cls[k]: print(l)
