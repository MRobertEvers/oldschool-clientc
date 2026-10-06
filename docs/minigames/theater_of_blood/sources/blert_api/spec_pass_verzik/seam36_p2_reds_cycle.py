import json,glob,os,collections
P2=(8372,10833,10850); REDS=(8385,10845,10862)
C=collections.Counter; between=C(); r2a=C(); a2r=C(); rr=C(); n=0
for f in sorted(glob.glob(os.environ['B']+'/blert_verzik/1[12]_*.json')):
    d=sorted(json.load(open(f)),key=lambda e:e['tick'])
    at=[e['tick'] for e in d if e['type']==10 and e['npc']['id'] in P2]
    reds=sorted({e['tick'] for e in d if e['type']==7 and 'npc' in e and e['npc']['id'] in REDS})
    if not reds or not at: continue
    n+=1
    for a,b in zip(reds,reds[1:]):
        ins=[t for t in at if a<t<b]
        if not ins: continue
        between[len(ins)]+=1; r2a[ins[0]-a]+=1; a2r[b-ins[-1]]+=1; rr[b-a]+=1
print('raids',n); print('attacks between reds',dict(between)); print('red->first attack',dict(r2a)); print('last attack->red',dict(a2r)); print('red->red',dict(rr))
