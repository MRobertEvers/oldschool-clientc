import sys
from collections import Counter, defaultdict
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import zparse
for path in sys.argv[1:]:
    faces = Counter(); lives = Counter(); tick_faces = defaultdict(list)
    for t, ev in zparse.parse(path):
        for e in ev:
            if e['k'] == 'npc' and e['mv'] == 'add':
                lives[e['name']] += 1
            if e['k'] == 'npcx' and e['blk'] in ('face_pathingentity', 'facing'):
                faces[e['name']] += 1
    print(path.rsplit('/',1)[-1])
    for n, c in faces.most_common(25):
        print('  %-40s faces %5d lives %4d  per life %.1f' % (n, c, lives[n], c / max(1, lives[n])))
