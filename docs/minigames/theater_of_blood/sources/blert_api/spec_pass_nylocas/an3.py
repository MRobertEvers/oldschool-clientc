import sys,collections
rows=[]
for l in open(sys.argv[1]):
    p=l.rstrip('\n').split('\t')
    if len(p)<9 or p[0].startswith('ticklog'): continue
    try: rows.append((int(p[0]),int(p[1]),p[2],[int(x) for x in p[3:9]]))
    except: pass
rows.sort()
SUP={8358,10790,10811}
sup=[r for r in rows if r[2]=='npc_spawn' and r[3][1] in SUP]
start=sup[0][1]
print('supports',len(sup),'type',sup[0][3][1],'start',start)
types=collections.Counter(r[3][1] for r in rows if r[2]=='npc_spawn')
print('spawn types',dict(types))
prince=[r for r in rows if r[2]=='npc_spawn' and r[3][1] in (10803,10804,10805,10806)]
print('prince spawn ticks',[r[1]-start for r in prince])
nyl=lambda t: 8342<=t<=8353 or 10774<=t<=10785 or 10791<=t<=10802
spw=collections.OrderedDict()
for r in rows:
    if r[2]=='npc_spawn' and nyl(r[3][1]): spw.setdefault(r[1]-start,0); spw[r[1]-start]+=1
wt=list(spw.items())
print('wave ticks',wt[:12],'...')
print('gaps',[wt[i+1][0]-wt[i][0] for i in range(len(wt)-1)])
print('total',sum(spw.values()),'waves',len(wt))
bt=[r for r in rows if r[2]=='npc_spawn' and r[3][1] in (8354,10786,10807)]
print('boss spawn',[ (r[1]-start,r[3][1]) for r in bt])
fr=[r[1] for r in rows if r[2]=='npc_free' and nyl(r[3][1])]
if bt: print('cleanup end',max(f for f in fr if f<bt[0][1])-start,'delta',bt[0][1]-max(f for f in fr if f<bt[0][1]))
# count in room at each wave tick (alive nylos, small=1, prince=3)
alive={}
live=collections.OrderedDict()
ev=[]
for r in rows:
    if r[2]=='npc_spawn' and (nyl(r[3][1]) or r[3][1] in (10803,10804,10805,10806)): ev.append((r[1],1,r[3][0],3 if r[3][1] in(10803,10804,10805,10806) else 1))
    if r[2]=='npc_free' and (nyl(r[3][1]) or r[3][1] in (10803,10804,10805,10806)): ev.append((r[1],0,r[3][0],0))
cnt={}
tot=0
bytick=collections.defaultdict(list)
for e in ev: bytick[e[0]].append(e)
counts={}
for t in sorted(bytick):
    counts[t]=None
# count before each wave spawn tick
cur=0;w={}
state={}
for t in sorted(bytick):
    before=sum(state.values())
    if any(e[1]==1 for e in bytick[t]) and t-start in spw and spw[t-start]>=0: counts[t]=before
    for e in sorted(bytick[t],key=lambda x:x[1]):
        if e[1]==0: state.pop(e[2],None)
    for e in bytick[t]:
        if e[1]==1: state[e[2]]=e[3]
stalls=[(t-start,c) for t,c in counts.items() if c is not None]
print('count just before each spawn tick (rel,count)',stalls[:45])
