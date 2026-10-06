#!/usr/bin/env python3
"""Jal-Zek (npc 7699 'mager') analysis of observed_npc_events.tsv -> MAGER_ANALYSIS.txt. OBSERVED events only:
NPC_ATTACK id 81 auto (anim 7610), 82 melee (7612), 83 resurrect (7611). Tick 0 = the `Wave: N` chat tick."""
import csv, collections, os
D=__import__('os').path.dirname(__import__('os').path.abspath(__file__))+'/'
allrows=list(csv.DictReader(open(D+'observed_npc_events.tsv'),delimiter='\t'))
def pct(a,p):
    a=sorted(a); return a[min(len(a)-1,int(p*len(a)))]
rows=[r for r in allrows if r['npc_id']=='7699']
by=collections.defaultdict(list)
for r in rows: by[(r['run'],r['wave'],r['room_id'])].append(r)
ids=collections.Counter(r['attack_id'] for r in rows if r['kind']=='attack')
print('mager (7699) spawn rows %d; zuk-set mager (7703) spawn rows %d'%(len(by),sum(1 for r in allrows if r['npc_id']=='7703' and r['kind']=='spawn')))
print('attack ids on 7699: %s'%dict(ids))
gapauto=collections.Counter();gapany=collections.Counter();after_res=collections.Counter();before_res=collections.Counter()
first=[];life=[];spawn0=collections.Counter();late=collections.Counter();perwave=collections.Counter();noact=0
resper=collections.Counter();firstkind=collections.Counter();after_melee=collections.Counter()
for k,v in by.items():
    s=[r for r in v if r['kind']=='spawn'][0]
    ev=sorted((int(r['tick']),r['attack_id']) for r in v if r['kind']=='attack')
    d=[int(r['tick']) for r in v if r['kind']=='death']
    if not ev: noact+=1
    nres=0
    for i in range(1,len(ev)):
        g=ev[i][0]-ev[i-1][0]; gapany[g]+=1
        if ev[i][1]=='81' and ev[i-1][1]=='81': gapauto[g]+=1
        if ev[i-1][1]=='83': after_res[g]+=1
        if ev[i][1]=='83': before_res[g]+=1
        if ev[i-1][1]=='82': after_melee[g]+=1
    resper[sum(1 for e in ev if e[1]=='83')]+=1
    if int(s['tick'])==0:
        spawn0[(int(s['x'])-2240,int(s['y'])-5312)]+=1; perwave[(k[0],k[1])]+=1
        if ev: first.append(ev[0][0]); firstkind[ev[0][1]]+=1
        if d: life.append(d[0])
    else: late[int(k[1])]+=1
H=lambda c,n=14:' '.join('%d:%d'%kv for kv in sorted(c.items())[:n])
print('mager that never acted before dying: %d of %d'%(noact,len(by)))
print('gaps between two consecutive auto attacks (81->81): total %d; gap 4: %d; gap<4: %d; 5-9: %d; >=10: %d'%(sum(gapauto.values()),gapauto[4],sum(c for g,c in gapauto.items() if g<4),sum(c for g,c in gapauto.items() if 5<=g<=9),sum(c for g,c in gapauto.items() if g>=10)))
print('auto gap histogram %s'%H(gapauto))
print('gap any two consecutive attacks: total %d min %d histogram %s'%(sum(gapany.values()),min(gapany),H(gapany,12)))
print('gap AFTER a resurrect to the next attack: total %d histogram %s'%(sum(after_res.values()),H(after_res,14)))
print('gap BEFORE a resurrect from the previous attack: total %d histogram %s'%(sum(before_res.values()),H(before_res,14)))
print('gap after a melee attack: total %d histogram %s'%(sum(after_melee.values()),H(after_melee,10)))
print('resurrects per mager life: %s'%dict(sorted(resper.items())))
print('first attack kind (tick-0 spawns): %s'%dict(firstkind))
print('first attack tick (tick-0 spawns): n %d min %d p10 %d median %d p90 %d max %d'%(len(first),min(first),pct(first,.1),pct(first,.5),pct(first,.9),max(first)))
print('death tick (tick-0 spawns): n %d min %d p10 %d median %d p90 %d max %d'%(len(life),min(life),pct(life,.1),pct(life,.5),pct(life,.9),max(life)))
print('tick-0 spawn tiles local x,z:count %s'%' '.join('%d,%d:%d'%(x,z,c) for (x,z),c in sorted(spawn0.items())))
pw=collections.defaultdict(set)
for (run,w),c in perwave.items(): pw[int(w)].add(c)
print('mager count by wave: %s'%' '.join('w%d:%s'%(w,'/'.join(map(str,sorted(c)))) for w,c in sorted(pw.items())))
print('late (revived) mager spawn rows by wave: %s'%(' '.join('w%d:%d'%kv for kv in sorted(late.items())) or 'none'))
# resurrect -> spawn of any other npc at a later tick, same run+wave, in the revive zone
byw=collections.defaultdict(list)
for r in allrows: byw[(r['run'],r['wave'])].append(r)
off=collections.Counter();tiles=collections.Counter();ntype=collections.Counter();nres=0;matched=0;firstatk=collections.Counter()
for k,v in byw.items():
    res=sorted(int(r['tick']) for r in v if r['npc_id']=='7699' and r['kind']=='attack' and r['attack_id']=='83')
    sp=[r for r in v if r['kind']=='spawn' and int(r['tick'])>0]
    for t in res:
        nres+=1
        c=[r for r in sp if t<=int(r['tick'])<=t+12 and r['npc_id']!='7709']
        if c:
            matched+=1
            r=min(c,key=lambda r:int(r['tick']))
            off[int(r['tick'])-t]+=1; tiles[(int(r['x'])-2240,int(r['y'])-5312)]+=1; ntype[r['npc']]+=1
            ar=[int(a['tick']) for a in v if a['room_id']==r['room_id'] and a['kind']=='attack']
            if ar: firstatk[min(ar)-int(r['tick'])]+=1
print('resurrect events %d; followed within 12 ticks by a later spawn row: %d'%(nres,matched))
print('resurrect->spawn tick offset: %s'%H(off))
print('revived spawn tiles local x,z:count %s'%' '.join('%d,%d:%d'%(x,z,c) for (x,z),c in sorted(tiles.items(),key=lambda kv:-kv[1])[:12]))
print('revived npc kinds: %s'%dict(ntype))
print('revived monster first attack minus its spawn tick: %s'%H(firstatk,14))

fa=[]
for k,c in firstatk.items(): fa+= [k]*c
fa.sort()
print('revived monster first attack minus spawn tick: n %d min %d p10 %d median %d p90 %d max %d (of %d revived; the rest never attacked or died first)'%(len(fa),fa[0],pct(fa,.1),pct(fa,.5),pct(fa,.9),fa[-1],nres))
