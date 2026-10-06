import sys,re,collections
name=sys.argv[1]
L=open('build/quest_gate/%s/ledger.tsv'%name).read().split('\n')
rows=[l for l in L if '\trows\t' in l][0].split('\t')[-1].split(' ')
ev=[]
for r in rows:
    p=r.split('@')
    if len(p)<2: continue
    kind=p[0]; rest=p[1].split(':')
    ev.append((kind,int(rest[0]),rest[1:]))
def show(kind,filt=None):
    return [(t,x) for k,t,x in ev if k==kind and (filt is None or filt(x))]
anims=[(t,int(x[1])) for k,t,x in ev if k=='npc_anim' and x[1] not in ('nil','')]
print("anims:", ' '.join('%d:%d'%a for a in anims if a[1] in (8109,8114,8115,8116,8117,8118,8119,8123,8124,8125,8126,8127,14406,8110,8111,8112)))
print("retype:",[(t,x[1],x[2]) for k,t,x in ev if k=='npc_retype' and x[0]=='1079' or (k=='npc_retype' and x[0]!='1079' and False)])
pr=[(t,x[1],x[2],x[3]) for k,t,x in ev if k=='projectile']
print("proj:", ' '.join('%d:%s:%s:%s'%p for p in pr))
ms=[(t,x[1],x[2]) for k,t,x in ev if k=='map_spotanim']
print("mapspot:", ' '.join('%d:%s:%s'%m for m in ms))
sp=[(t,x[0],x[1]) for k,t,x in ev if k=='npc_spawn']
print("spawn:", ' '.join('%d:%s:%s'%s for s in sp))
hp=[(t,x[1]) for k,t,x in ev if k=='hit_player']
print("hits:", ' '.join('%d:%s'%h for h in hp))
