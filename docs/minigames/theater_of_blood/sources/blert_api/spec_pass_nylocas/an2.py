import sys,collections
rows=[]
for l in open(sys.argv[1]):
    p=l.rstrip('\n').split('\t')
    if len(p)<9 or p[0].startswith('ticklog'): continue
    try: rows.append((int(p[0]),int(p[1]),p[2],[int(x) for x in p[3:9]]))
    except: pass
rows.sort()
# pair spawn->events by ordered lifetimes per slot
life=[]  # (slot,type,spawn_tick,free_tick,det_tick)
cur={}
for ser,t,k,a in rows:
    if k=='npc_spawn' and a[1]!=8358:
        cur[a[0]]=[a[0],a[1],t,None,None,0]
    elif k=='npc_anim' and a[0] in cur and a[2] in (7992,8000,8006) and cur[a[0]][4] is None:
        cur[a[0]][4]=t
    elif k=='npc_free' and a[0] in cur and cur[a[0]][3] is None:
        cur[a[0]][3]=t; life.append(cur[a[0]])
sm=[x for x in life if x[1] in range(8342,8354)]
print(len(sm),'nylos with free')
print('spawn->free',collections.Counter(x[3]-x[2] for x in sm))
print('spawn->det anim',collections.Counter((x[4]-x[2]) if x[4] else None for x in sm))
print('det anim->free',collections.Counter((x[3]-x[4]) if x[4] else None for x in sm))
# hits on player: group by npc_type, damage
hp=[(t,a) for ser,t,k,a in rows if k=='hit_player']
print('hit_player n',len(hp))
by=collections.defaultdict(list)
for t,a in hp: by[a[5]].append(a[2])
for ty,v in sorted(by.items()): print(ty,len(v),'max',max(v),'min',min(v),'mean',round(sum(v)/len(v),2))
# explosion hits: hit_player on same tick as free of nylo of that slot
fr={}
for x in sm: fr.setdefault((x[2],x[3]),x)
