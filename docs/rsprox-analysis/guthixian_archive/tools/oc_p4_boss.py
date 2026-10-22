# Ocellus phase 4 (oc_p4.py): his own npc-info blocks by D over every full cycle.
# Run from this directory: python3 oc_p4_boss.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import defaultdict, Counter
fs = oc_p4.load()
rows = defaultdict(Counter)   # (d, text) -> sessions
nd = Counter()
for f in fs:
    for D in f.D:
        if f.K and D + 59 >= f.K: continue
        nd['cycles'] += 1
        for d in range(-3, 60):
            T = f.t.get(D + d); b = T and T.npc.get(f.boss)
            if not b: continue
            items = ['seq %s/%d' % s for s in b.seqs] + ['spot s%d %s%s' % (s[0], s[1], s[2].replace(', height=', ' h')) for s in b.spots if 'thrall' not in s[1] and 'glaive' not in s[1] and 'sang' not in s[1] and s[1] not in ('blood_spell_heal',)]
            items += ['face ' + re.sub(r'player=\(index=\d+, name=([^,]*), coord=\([^)]*\)\)', r'player=\1', x).replace(', walktype=turnonwalk', '') for x in b.faces]
            if b.icons is not None: items.append('icons %s' % b.icons)
            items += [o for o in b.other if not o.startswith('[headbar]') and not o.startswith('[hit]')]
            if b.say: items.append('say %s' % b.say)
            for it in items:
                it = re.sub(r"player=.*", "player=X", it)
                rows[(d, it)][f.sid] += 1
print(nd)
for (d, it), c in sorted(rows.items()):
    n = sum(c.values())
    if 'player=X' in it and 'fallback' in it: it = re.sub(r'fallbackangle=\d+', '', it)
    print('%3d %3d %s' % (d, n, it[:150]))
