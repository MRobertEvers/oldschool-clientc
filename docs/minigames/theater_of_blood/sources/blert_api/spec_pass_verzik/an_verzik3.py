import json,glob,collections,os
C=collections.Counter
P2=(8372,10833,10850); P3=(8374,10835,10852)
CRABS=(8381,8382,8383,10841,10842,10843,10858,10859,10860); PURPLE=(8384,10844,10861)
res=collections.defaultdict(list)
for f in sorted(glob.glob('blert_verzik/*.json')):
    mode,scale=map(int,os.path.basename(f).split('_')[:2]); d=sorted(json.load(open(f)),key=lambda e:e['tick'])
    tag={10:'entry',11:'n',12:'h'}[mode]
    ph=[e['tick'] for e in d if e['type']==150]
    sp=[(e['tick'],e['npc']['roomId']) for e in d if e['type']==7 and e['npc']['id'] in CRABS]
    # cluster
    clusters=[]
    for t,r in sp:
        if clusters and t-clusters[-1][-1][0]<=2: clusters[-1].append((t,r))
        else: clusters.append([(t,r)])
    for c in clusters:
        phase='p2' if (len(ph)>1 and c[0][0]<ph[1]) else 'p3'
        res['cast_size_%s_%s_sc%d'%(tag,phase,scale)].append(len(c))
    # purple attack ticks vs crab cluster start
    pa=[e['tick'] for e in d if e['type']==10 and e['npc']['id'] in P2 and e['npcAttack']['attack']==16]
    for t in pa:
        nxt=[c[0][0] for c in clusters if c[0][0]>=t-2]
        if nxt: res['crab_minus_purple_attack_'+tag].append(nxt[0]-t)
    # crab lifetime
    born={}; 
    for e in d:
        if e['type']==7 and e['npc']['id'] in CRABS: born[e['npc']['roomId']]=e['tick']
        if e['type']==9 and e['npc']['id'] in CRABS and e['npc']['roomId'] in born:
            res['crab_life_'+tag].append(e['tick']-born.pop(e['npc']['roomId']))
    psl=[e['tick'] for e in d if e['type']==7 and e['npc']['id'] in PURPLE]
    for t in pa:
        n=[st-t for st in psl if st>=t-1]
        if n: res['purple_spawn_minus_attack_'+tag].append(n[0])
    bornp={}
    for e in d:
        if e['type']==7 and e['npc']['id'] in PURPLE: bornp[e['npc']['roomId']]=e['tick']
        if e['type']==9 and e['npc']['id'] in PURPLE and e['npc']['roomId'] in bornp:
            res['purple_life_'+tag].append(e['tick']-bornp.pop(e['npc']['roomId']))
for k in sorted(res):
    v=res[k]; print(k, dict(sorted(C(v).items())))

# sanity: casts never exceed party size
bad=0; total=0
for k,v in res.items():
    if k.startswith('cast_size_'):
        sc=int(k.rsplit('sc',1)[1])
        for x in v:
            total+=1
            if x>sc: bad+=1
print('casts_total',total,'casts_over_party_size',bad)
print('crab_life_total_n',len(res['crab_life_n']),'ge26',len([x for x in res['crab_life_n'] if x>=26]))
