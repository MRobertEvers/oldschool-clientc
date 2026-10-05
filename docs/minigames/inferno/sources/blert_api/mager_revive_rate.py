#!/usr/bin/env python3
"""Revive rate and which corpse: resurrect events (id 83) over mager action opportunities with >=1 revivable corpse.
Eligible corpse npc ids: bat 7692, blob 7693, meleer 7697, ranger 7698, mager 7699 (not nibbler 7691, not bloblets 7694-7696,
not pillars). A revived instance (spawn row with tick>0 at a resurrect tick of the same wave) never becomes a corpse again."""
import csv,collections
D=__import__('os').path.dirname(__import__('os').path.abspath(__file__))+'/'
rows=list(csv.DictReader(open(D+'observed_npc_events.tsv'),delimiter='\t'))
byw=collections.defaultdict(list)
for r in rows: byw[(r['run'],r['wave'])].append(r)
EL={'7692','7693','7697','7698','7699'}
opp=0;res=0;rec=collections.Counter();corpse_kinds=collections.Counter();pick=collections.Counter();nib_pick=0
opp_by_n=collections.defaultdict(lambda:[0,0]);tiles=[];waves=collections.Counter()
for k,v in byw.items():
    revived=set()
    ress=sorted((int(r['tick']),r['room_id']) for r in v if r['npc_id']=='7699' and r['kind']=='attack' and r['attack_id']=='83')
    for t,_ in ress:
        for r in v:
            if r['kind']=='spawn' and int(r['tick'])==t and r['npc_id']!='7709' and int(r['tick'])>0: revived.add(r['room_id']); tiles.append((int(r['x'])-2240,int(r['y'])-5312))
    deaths=sorted((int(r['tick']),r['npc_id'],r['room_id']) for r in v if r['kind']=='death' and r['npc_id'] in EL and r['room_id'] not in revived)
    acts=sorted((int(r['tick']),r['attack_id'],r['room_id']) for r in v if r['npc_id']=='7699' and r['kind']=='attack')
    used=0
    for t,a,rm in acts:
        n=sum(1 for d in deaths if d[0]<t)-used
        if n>0:
            opp+=1; o=opp_by_n[min(n,3)]; o[0]+=1
            if a=='83': o[1]+=1; res+=1
        if a=='83': used+=1
print('mager action events with >=1 eligible corpse available: %d; resurrects among them %d (%.1f%%)'%(opp,res,100.0*res/max(opp,1)))
print('by corpses available (1,2,3+): '+' '.join('%d: %d/%d=%.1f%%'%(n,x[1],x[0],100.0*x[1]/max(x[0],1)) for n,x in sorted(opp_by_n.items())))
xs=[t[0] for t in tiles];zs=[t[1] for t in tiles]
print('revived spawn tiles n %d: x %d-%d z %d-%d (local)'%(len(tiles),min(xs),max(xs),min(zs),max(zs)))

# which corpse: compare the revived npc id with the oldest / newest eligible corpse not yet revived.
oldest=newest=tot=multi=randexp=0
for k,v in byw.items():
    ress=sorted(int(r['tick']) for r in v if r['npc_id']=='7699' and r['kind']=='attack' and r['attack_id']=='83')
    rev={}
    for t in ress:
        for r in v:
            if r['kind']=='spawn' and int(r['tick'])==t and r['npc_id']!='7709' and int(r['tick'])>0: rev[t]=r
    revids={r['room_id'] for r in rev.values()}
    deaths=sorted((int(r['tick']),r['npc_id']) for r in v if r['kind']=='death' and r['npc_id'] in EL and r['room_id'] not in revids)
    pool=list(deaths)
    for t in ress:
        if t not in rev: continue
        avail=[d for d in pool if d[0]<t]
        if len(avail)<2: 
            if avail: pool.remove(avail[0])
            continue
        multi+=1
        want=rev[t]['npc_id']
        # revived npc ids use the same ids as the corpse's; blob 7693 splits are excluded already
        if avail[0][1]==want: oldest+=1
        if avail[-1][1]==want: newest+=1
        randexp+=sum(1 for d in avail if d[1]==want)/len(avail)
        for d in avail:
            if d[1]==want: pool.remove(d); break
print('which corpse (events with >=2 eligible corpses waiting: %d): revived type = oldest corpse type %d, = newest %d, expected by a uniform pick %.1f'%(multi,oldest,newest,randexp))
