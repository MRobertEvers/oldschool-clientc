# Ocellus phase 4 (oc_p4.py): his death `angle=` against his last
# `[facing] player=` block: the target's tile from his middle, and the
# block's own fallbackangle (his turned orientation), each to 8 ways.
# Run from this directory: python3 oc_p4_deathangle.py
import sys, re, math; sys.path.insert(0, '.')
import oc_p4
q8 = lambda a: int(round(a / 256)) % 8 * 256
for f in oc_p4.load():
    if not f.K: continue
    K = f.K; b = f.ticks[K].npc[f.boss]; mid = (b.before[0] + 1, b.before[1] + 1)
    death = [int(x.split('=')[1]) for x in b.faces if x.startswith('angle=')]
    last = None
    for t in range(K - 120, K):
        bb = f.ticks.get(t) and f.ticks[t].npc.get(f.boss)
        for x in (bb.faces if bb else []):
            m = re.search(r'coord=\((\d+), (\d+), \d\)\), fallbackangle=(\d+)', x)
            if 'player=' in x and m: last = (t - K, int(m.group(1)), int(m.group(2)), int(m.group(3)))
    tile = (math.degrees(math.atan2(-(last[1] - mid[0]), -(last[2] - mid[1]))) % 360) / 360 * 2048
    print(f.sid, K, 'death', death, 'tile', q8(tile), 'fallback', last[3], '->', q8(last[3]), 'at K%+d' % last[0])
