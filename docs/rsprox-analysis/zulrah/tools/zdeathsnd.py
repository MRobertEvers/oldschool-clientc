import sys
from collections import Counter
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import zparse
c = Counter(); n = 0
for path in sys.argv[1:]:
    ticks = list(zparse.parse(path))
    for i, (t, ev) in enumerate(ticks):
        for e in ev:
            if e['k'] == 'npcx' and e['blk'] == 'sequence' and 'snakeboss_death' in e['body']:
                n += 1
                D = t
                for t2, ev2 in ticks[i:i + 12]:
                    for e2 in ev2:
                        if e2['k'] in ('sarea', 'synth'):
                            c[(t2 - D, e2['k'], e2['id'], e2.get('range', ''))] += 1
print('deaths', n)
for k, v in sorted(c.items()):
    print(v, k)
