import sys
from collections import Counter
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import zparse
ends = Counter(); ex = []
for path in sys.argv[1:]:
    live = {}
    for t, ev in zparse.parse(path):
        for e in ev:
            if e['k'] == 'locadd' and e['shape'] == 10:
                key = (e['x'], e['z'])
                if key in live:
                    a = live.pop(key)
                    ends['replaced by %s at +%d' % (e['name'][:12], t - a[0])] += 1
                if e['name'] == 'snakeboss_poisoncloud':
                    live[key] = (t, e['rot'])
            elif e['k'] == 'locdel' and e['shape'] == 10:
                key = (e['x'], e['z'])
                if key in live:
                    a = live.pop(key)
                    ends['del +%d rot %s' % (t - a[0], 'same' if a[1] == e['rot'] else 'cloud %d del %d' % (a[1], e['rot']))] += 1
for k, v in ends.most_common(20):
    print('%5d %s' % (v, k))
