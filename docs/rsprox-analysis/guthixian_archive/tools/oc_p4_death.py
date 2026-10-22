# Ocellus phase 4 (oc_p4.py): his and the room blocks around his death (K).
# Run from this directory: python3 oc_p4_death.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
from collections import defaultdict, Counter
rows = defaultdict(Counter)
for f in oc_p4.load():
    if not f.K: continue
    for d in range(-1, 18):
        T = f.ticks.get(f.K + d)
        if not T: continue
        for i, n in T.npc.items():
            if 'thrall' in n.type or 'pet' in n.type: continue
            who = 'BOSS' if i == f.boss else n.type
            items = ['seq %s/%d' % s for s in n.seqs] + ['spot s%d %s%s' % (s[0], s[1], s[2]) for s in n.spots if 'thrall' not in s[1] and s[1] != '-1' or (s[1] == '-1' and 'delay=77' not in s[2])]
            items += ['face ' + re.sub(r'player=.*', 'player', x) for x in n.faces]
            if n.icons is not None: items.append('icons %s' % n.icons)
            items += [o for o in n.other if not o.startswith('[headbar]') and not o.startswith('[hit]')]
            if n.move in ('add', 'del'): items.append(n.move)
            for it in items: rows[(d, who)][it[:110]] += 1
        for k, s in T.top:
            if 'message_game' in s or 'midi' in s or 'loc_' in s:
                rows[(d, 'top')][re.sub(r'\d+:\d\d', 'm:ss', s)[:110]] += 1
for k in sorted(rows):
    for it, n in rows[k].items(): print(k[0], k[1][:22], n, it)
