"""ez_stepswing.py on our tick log, with the attack gaps.

    python3 ez_stepswing_ours.py <ticklog.tsv>
"""
import sys
def edge(b,p):
    x0,z0=b; px,pz=p
    return ((x0<=px<=x0+2) and pz in (z0-1,z0+3)) or ((z0<=pz<=z0+2) and px in (x0-1,x0+3))
boss={}; pl={}; seqs={}; slot=None
for line in open(sys.argv[1]):
    f=line.rstrip("\n").split("\t")
    if len(f)<9 or not f[1].isdigit(): continue
    t=int(f[1]); k=f[2]; v=f[3:]
    if k=="npc_spawn" and v[1]=="16652": slot=v[0]; c=int(v[2]); boss[t]=((c>>14)&16383,c&16383)
    if k=="npc_tile" and v[0]==slot: boss[t]=(int(v[1]),int(v[2]))
    if k=="player_tile": pl[t]=(int(v[1]),int(v[2]))
    if k=="npc_anim" and v[1]=="16652" and v[2] in ("14685","14687","14686"): seqs[t]={"14685":"melee","14687":"special","14686":"magic"}[v[2]]
def at(d,t):
    ks=[k for k in d if k<=t]; return d[max(ks)] if ks else None
att=sorted(seqs)
prev=None
gaps=[]
for a in att:
    if prev: gaps.append((a, a-prev, seqs[prev], seqs[a]))
    prev=a
for k in sorted(boss):
    if at(boss,k-1)==boss[k] or at(boss,k-1) is None: continue
    p0,p1=at(pl,k-1),at(pl,k)
    if not p0 or not p1 or edge(at(boss,k-1),p0) or not edge(boss[k],p1): continue
    last=max([a for a in att if a<k],default=None)
    if last is None: continue
    nxt=min([a for a in att if a>=k],default=None)
    print(k,"since",k-last,seqs[last],"-> +",None if nxt is None else nxt-k,seqs.get(nxt))
print("gaps", [(g[1],g[2][0]+g[3][0]) for g in gaps])
