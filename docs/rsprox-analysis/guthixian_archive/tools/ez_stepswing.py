"""Every step that put the tank edge-on with an attack due, and the attack after
it (§3: the swing on the step's tick when due; magic at due+1 on the reach
before the step). On --all-players transcripts.

    python3 ez_stepswing.py <transcript>...
"""
import re,sys
def edge(b,p):
    x0,z0=b; px,pz=p
    inx = x0<=px<=x0+2; inz = z0<=pz<=z0+2
    return (inx and pz in (z0-1,z0+3)) or (inz and px in (x0-1,x0+3))
for path in sys.argv[1:]:
    t=0; boss={}; pl={}; tank=None; tankat={}; seqs={}; cur=None
    for line in open(path,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); continue
        m=re.match(r"^ {8}\[\w+\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m: pl.setdefault(m[1],{})[t]=(int(m[4] or m[2]),int(m[5] or m[3])); cur=None; continue
        m=re.match(r"^ {8}\[\w+\] npc=\(index=\d+, id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            cur = m[1]=="tfa_ezekiel"
            if cur: boss[t]=(int(m[4] or m[2]),int(m[5] or m[3]))
            continue
        if cur:
            m=re.search(r"\[facing\] player=\(index=\d+, name=([^,]+),",line)
            if m: tank=m[1]
            tankat[t]=tank
            m=re.search(r"\[sequence\] id=npc_ezekiel_(\w+) ",line)
            if m: seqs[t]=m[1]
    def at(d,tt):
        ks=[k for k in d if k<=tt]
        return d[max(ks)] if ks else None
    attacks=sorted(k for k,v in seqs.items() if v in("melee01","special01","magic01"))
    for k in sorted(boss):
        if k-1 not in boss and at(boss,k-1)==boss[k]: continue
        if at(boss,k-1)==boss[k]: continue
        tk=at(tankat,k)
        if not tk or tk not in pl: continue
        pb=at(boss,k-1); p0=at(pl[tk],k-1); p1=at(pl[tk],k)
        if not p0 or not p1 or edge(pb,p0) or not edge(boss[k],p1): continue
        last=max([a for a in attacks if a<k],default=None)
        if last is None or k-last<5: continue
        nxt=min([a for a in attacks if a>=k],default=None)
        print(path.split('/')[-1],k,"since last",k-last,seqs.get(last),"-> next attack +",None if nxt is None else nxt-k, seqs.get(nxt),"tank moved" if p0!=p1 else "")
