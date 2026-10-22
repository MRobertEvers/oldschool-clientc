"""Every finale strike against the volley rule (§8): the two lit swords, the safe
tiles, impacts at 124 + 30k + 2 a row. Transcripts or our tick log.

    python3 ez_volley_rule.py <transcript|ticklog.tsv>...
"""
import re,sys
from collections import defaultdict
CAGE=[(x,z) for x in range(4062,4065) for z in range(5534,5537)]
def side_of(b):
    # his sw relative to the cage's sw (4062,5534)
    dx,dz=b[0]-4062,b[1]-5534
    if dz<=-3: return "S"
    if dz>=3: return "N"
    if dx>=3: return "E"
    return "W"
def frame(side):
    # (row index from his side, column) -> tile; left/right as he faces the cage
    if side=="S": fwd=(0,1); left=(-1,0)
    if side=="N": fwd=(0,-1); left=(1,0)
    if side=="E": fwd=(-1,0); left=(0,-1)
    if side=="W": fwd=(1,0); left=(0,1)
    cx,cz=4063,5535
    tiles={}
    for r in range(3):
        for c in (-1,0,1):  # c=-1 left column, +1 right
            x=cx+fwd[0]*(r-1)+left[0]*(-c); z=cz+fwd[1]*(r-1)+left[1]*(-c)
            tiles[(x,z)]=(r,c)
    return tiles
def safe(lit,fr):
    s=set()
    for t,(r,c) in fr.items():
        if lit=={"L","R"} and r==2: s.add(t)
        if lit=={"F","L"} and c==1 and r>=1: s.add(t)
        if lit=={"F","R"} and c==-1 and r>=1: s.add(t)
    return s
def check(vols,label):
    ok=bad=0
    for V,(b,lits,imp) in sorted(vols.items()):
        side=side_of(b); fr=frame(side)
        for k,lit in sorted(lits.items()):
            exp={t:124+30*k+2*fr[t][0] for t in fr if t not in safe(lit,fr)}
            got={t:d for (t,d,kk) in imp if kk==k}
            if exp==got: ok+=1
            else:
                bad+=1
                print(label,V,"k",k,side,sorted(lit),"exp",sorted((t[0]-4062,t[1]-5534,d) for t,d in exp.items()),"got",sorted((t[0]-4062,t[1]-5534,d) for t,d in got.items()))
    print(label,"strikes ok",ok,"bad",bad)
def parse_rec(p):
    vols={}; t=0; boss={}; ids={}
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        m=re.search(r"npc=\(index=(\d+), id=(\w+) \(\d+\), coord=\((\d+), (\d+)",line)
        if m:
            cur=m[1]
            if m[2]=="tfa_ezekiel": boss[t]=(int(m[3]),int(m[4]))
            continue
        m=re.search(r"\[sequence\] id=vfx_ezekiel_finale_sword_drop0(\d)",line)
        if m and cur: ids[cur]={"1":"F","3":"L","4":"R"}[m[1]]
        m=re.search(r"\[spotanim\](?: slot=(\d),)? id=vfx_ezekiel_sword_telegraph0\d \(\d+\)",line)
        if m and cur in ids:
            v=vols.setdefault(t,[None,defaultdict(set),[]]); v[1][int(m[1] or 0)].add(ids[cur])
            v[0]=max((k for k in boss if k<=t)); v[0]=boss[v[0]]
        m=re.search(r"\[map_anim\] id=vfx_ezekiel_finale_impact_\w+ \(\d+\), delay=(\d+), coord=\((\d+), (\d+)",line)
        if m:
            V=max(k for k in vols if k<=t) if vols else None
            if V is not None and t-V<=6:
                d=int(m[1]); vols[V][2].append(((int(m[2]),int(m[3])),d,(d-124)//30))
    return vols
def parse_ours(p):
    vols={}; boss={}; slotb=None; ids={}
    for line in open(p):
        f=line.rstrip("\n").split("\t")
        if len(f)<9 or not f[1].isdigit(): continue
        t=int(f[1]); k=f[2]; v=f[3:]
        conv=lambda c:(((c>>14)&16383)-2368,(c&16383)+5376)
        if k=="npc_spawn" and v[1]=="16652": slotb=v[0]; boss[t]=conv(int(v[2]))
        if k=="npc_tile" and v[0]==slotb: boss[t]=(int(v[1])-2368,int(v[2])+5376)
        if k=="npc_anim" and v[1]=="16655": ids[v[0]]={"14710":"F","14718":"L","14722":"R"}.get(v[2],ids.get(v[0]))
        if k=="npc_spotanim" and v[1]=="16655" and v[2] in("4182","4190","4194"):
            vv=vols.setdefault(t,[None,defaultdict(set),[]]); vv[1][int(v[5])].add(ids[v[0]])
            vv[0]=boss[max(x for x in boss if x<=t)]
        if k=="map_spotanim" and v[1] in("4197","4198"):
            V=max(x for x in vols if x<=t) if vols else None
            if V is not None and t-V<=6:
                d=int(v[3]); vols[V][2].append((conv(int(v[0])),d,(d-124)//30))
    return vols
for p in sys.argv[1:]:
    check(parse_ours(p) if p.endswith(".tsv") else parse_rec(p), p.split("/")[-1])
