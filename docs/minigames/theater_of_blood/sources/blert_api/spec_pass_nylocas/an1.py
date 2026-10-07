import sys,collections,statistics
path=sys.argv[1]
rows=[]
for l in open(path):
    p=l.rstrip('\n').split('\t')
    if len(p)<9 or p[0].startswith('ticklog') : continue
    try: rows.append((int(p[0]),int(p[1]),p[2],[int(x) for x in p[3:9]]))
    except: pass
sup=[r for r in rows if r[2]=='npc_spawn' and r[3][1]==8358]
start=sup[0][1]
print('supports spawn tick',[r[1] for r in sup],'start',start)
spawns=[r for r in rows if r[2]=='npc_spawn' and r[3][1]!=8358]
bytick=collections.OrderedDict()
for r in spawns: bytick.setdefault(r[1],[]).append(r)
# waves: group by tick, excluding boss types
wt=[(t,len(v)) for t,v in bytick.items() if all(x[3][1] not in (8354,8355,8356,8357) for x in v)]
print('wave ticks (rel)',[(t-start,n) for t,n in wt][:40])
gaps=[wt[i+1][0]-wt[i][0] for i in range(len(wt)-1)]
print('gaps',gaps)
print('total spawn',sum(n for t,n in wt),'distinct wave ticks',len(wt))
# lifetime: spawn -> free
free={r[3][0]:r[1] for r in rows if r[2]=='npc_free'}
sp={r[3][0]:r[1] for r in spawns}
lt=collections.Counter(free[s]-sp[s] for s in sp if s in free)
print('spawn->free lifetime',lt)
# detonate anim
det_seqs={7992,8000,8006}
dd=collections.Counter()
for r in rows:
    if r[2]=='npc_anim' and r[3][2] in det_seqs and r[3][0] in sp:
        dd[r[1]-sp[r[3][0]]]+=1
print('spawn->detonate anim',dd)
# attack cadence per slot
atk={7989,7999,8004}
by=collections.defaultdict(list)
for r in rows:
    if r[2]=='npc_anim' and r[3][2] in atk: by[r[3][0]].append(r[1])
g=collections.Counter()
for s,v in by.items():
    for a,b in zip(v,v[1:]): g[b-a]+=1
print('nylo attack gaps',g, 'slots',len(by))
# chew: hit_npc on supports
sl={r[3][0] for r in sup}
hits=[r for r in rows if r[2]=='hit_npc' and r[3][0] in sl]
dmg=collections.Counter(r[3][2] for r in hits)
print('pillar hit dmg dist',dmg,'n',len(hits),'mean',sum(k*v for k,v in dmg.items())/max(1,len(hits)))
# per slot first hit and last hit
for s in sl:
    h=[r for r in hits if r[3][0]==s]
    if h: print('support',s,'hits',len(h),'sum',sum(x[3][2] for x in h),'first',h[0][1]-start,'last',h[-1][1]-start)
# support deaths/free
for r in rows:
    if r[2] in('npc_death','npc_free') and r[3][1]==8358: print(r[2],r[1]-start,r[3][0])
# boss
for r in rows:
    if r[2]=='npc_spawn' and r[3][1] in (8354,8355,8356,8357): print('boss spawn',r[1]-start,r[3])
rt=[(r[1],r[3]) for r in rows if r[2]=='npc_retype' and r[3][2] in (8355,8356,8357,8354)]
print('boss retypes',[(t-start,a[1],a[2]) for t,a in rt])
