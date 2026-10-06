#!/usr/bin/env python3
"""Jal-Ak (7693) and Jal-AkRek-Mej/Xil/Ket (7694-7696) analysis of observed_npc_events.tsv -> BLOB_ANALYSIS.txt.
Run: python3 blob_analysis.py > BLOB_ANALYSIS.txt (2026-10-04, Inferno blob_and_splits spec worker). Observed events only;
tick 0 = the Wave: N chat tick (blert/PROVENANCE.md); blert y is north-up, offsets are (dx, dy) from the blob death position."""
import csv, collections, os
import os
P=os.path.join(os.path.dirname(os.path.abspath(__file__)),'observed_npc_events.tsv')
rows=list(csv.DictReader(open(P),delimiter='\t'))
runs=len(set(r['run'] for r in rows))
B={'7693':'blob','7694':'mej','7695':'xil','7696':'ket'}
by=collections.defaultdict(list)
for r in rows:
    if r['npc_id'] in B: by[(r['run'],r['wave'],r['npc_id'],r['room_id'])].append(r)
print('runs',runs)
def H(c): return ' '.join('%s:%d'%(k,c[k]) for k in sorted(c))
# blobs
perwave=collections.Counter(); gaps=collections.defaultdict(collections.Counter); first=collections.defaultdict(list); spawnt=collections.defaultdict(collections.Counter)
anim=collections.Counter(); deaths={}; spawns={}
for k,v in by.items():
    run,wave,nid,room=k; n=B[nid]
    s=[r for r in v if r['kind']=='spawn']; d=[r for r in v if r['kind']=='death']; a=sorted((int(r['tick']),r['attack']) for r in v if r['kind']=='attack')
    if s:
        st=int(s[0]['tick']); spawnt[n][st]+=1; spawns[k]=(st,int(s[0]['x']),int(s[0]['y']))
        if n=='blob': perwave[(run,wave)]+=1
        if a: first[n].append(a[0][0]-st)
    if d: deaths[k]=(int(d[0]['tick']),int(d[0]['x']),int(d[0]['y']))
    for t,at in a: anim[(n,at)]+=1
    for (x,_),(y,_) in zip(a,a[1:]): gaps[n][y-x]+=1
print('attack by kind',dict(anim))
for n in B.values():
    print(n,'spawn tick',H(spawnt[n]) if len(spawnt[n])<12 else 'many(%d)'%sum(spawnt[n].values()))
    f=sorted(first[n]);
    if f: print(n,'first attack after spawn n',len(f),'min',f[0],'p10',f[len(f)//10],'med',f[len(f)//2],'p90',f[len(f)*9//10],'max',f[-1])
    g=gaps[n]; tot=sum(g.values())
    print(n,'gaps total',tot,'hist',H(g) if len(g)<20 else H(dict(sorted(g.items())[:20])))
# per wave blob count
cw=collections.defaultdict(collections.Counter)
for (run,wave),c in perwave.items(): cw[int(wave)][c]+=1
print('blobs per wave',{w:dict(c) for w,c in sorted(cw.items())})
# blob death -> bloblet spawn delay and offsets, per run wave
delay=collections.Counter(); off=collections.defaultdict(collections.Counter)
bl=[(k,v) for k,v in deaths.items() if k[2]=='7693']
sp=collections.defaultdict(list)
for k,v in spawns.items():
    if k[2]!='7693': sp[(k[0],k[1])].append((k,v))
life=[]
for k,(dt,dx,dy) in bl:
    cand=[(kk,vv) for kk,vv in sp[(k[0],k[1])] if vv[0]>=dt-1 and vv[0]<=dt+6 and abs(vv[1]-dx)<=6 and abs(vv[2]-dy)<=6]
    for kk,vv in cand:
        delay[vv[0]-dt]+=1; off[B[kk[2]]][(vv[1]-dx,vv[2]-dy)]+=1
    if k in spawns: life.append(dt-spawns[k][0])
print('bloblet spawn minus blob death tick',H(delay))
for n,c in off.items(): print('offset(blert units) from blob death pos',n,dict(c.most_common(6)))
print('blob lifetime ticks n',len(life),'min',min(life),'median',sorted(life)[len(life)//2])
# blob spawn tiles
t=collections.Counter((spawns[k][1]-2240,spawns[k][2]-5312) for k in spawns if k[2]=='7693' and spawns[k][0]==0)
print('blob tick0 spawn tiles',dict(t))
# bloblet attacks: ticks since own spawn first; bloblets killed before attack
na=0;tot=0
for k,v in by.items():
    if k[2]!='7693':
        tot+=1
        if not any(r['kind']=='attack' for r in v): na+=1
print('bloblets with no attack',na,'of',tot)
print('--- extra')
st=collections.Counter(); 
for k,(t,x,y) in spawns.items():
    if k[2]=='7693': st['tick0' if t==0 else 'late']+=1
print('blob spawn tick0 vs late',dict(st))
late=collections.Counter(int(k[1]) for k,(t,x,y) in spawns.items() if k[2]=='7693' and t>0); print('late blob spawns by wave',dict(late))
n=collections.Counter()
for k,(t,x,y) in spawns.items():
    if k[2]!='7693': n[B[k[2]]]+=1
print('bloblet spawns by kind',dict(n),'blob deaths',sum(1 for k in deaths if k[2]=='7693'),'blobs',sum(1 for k in spawns if k[2]=='7693'))
# attack per blob
na=0;tot=0
for k,v in by.items():
    if k[2]=='7693':
        tot+=1
        if not any(r['kind']=='attack' for r in v): na+=1
print('blobs with no attack',na,'of',tot)
# melee share of blob attacks
mc=collections.Counter(r['attack'] for k,v in by.items() if k[2]=='7693' for r in v if r['kind']=='attack'); print('blob attack kinds',dict(mc))
# per-bloblet kinds attack share
ev=collections.Counter((B[k[2]],r['attack']) for k,v in by.items() if k[2]!='7693' for r in v if r['kind']=='attack'); print(dict(ev))
