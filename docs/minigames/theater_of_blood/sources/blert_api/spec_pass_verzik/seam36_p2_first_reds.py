import json,glob,os,collections
P2=(8372,10833,10850); REDS=(8385,10845,10862)
c=collections.Counter()
for f in sorted(glob.glob(os.environ['B']+'/blert_verzik/1[012]_*.json')):
    d=sorted(json.load(open(f)),key=lambda e:e['tick'])
    at=[e['tick'] for e in d if e['type']==10 and e['npc']['id'] in P2]
    reds=sorted({e['tick'] for e in d if e['type']==7 and 'npc' in e and e['npc']['id'] in REDS})
    if not reds or not at: continue
    pre=[t for t in at if t<reds[0]]
    if pre: c[(os.path.basename(f)[:2], reds[0]-pre[-1])]+=1
print(dict(c))
