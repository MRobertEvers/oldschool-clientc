"""The tile Bork faces as he is stunned against his target's tile.
    python3 bk_stunface.py <all-players transcript>...
"""
import re,sys
for p in sys.argv[1:]:
    t=0; cur=None; boss={}; pl={}; target={}; stun=None; out=[]
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m: pl.setdefault(m[2],{})[t]=(int(m[5] or m[3]),int(m[6] or m[4])); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            cur=m[2]
            if cur=="tfa_bork": boss[t]=(int(m[5] or m[3]),int(m[6] or m[4]))
            continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_bork":
            mm=re.search(r"\[facing\] player=\(index=\d+, name=([^,]+),",line)
            if mm: target[t]=mm[1]
            if "stun01" in line: stun=t
            mm=re.search(r"\[facing\] coord=\((\d+), (\d+)",line)
            if mm and stun is not None and t-stun<=1:
                out.append((stun,t,(int(mm[1]),int(mm[2]))))
    def at(d,tt):
        ks=[k for k in d if k<=tt]; return d[max(ks)] if ks else None
    for L,t,c in out:
        sw=at(boss,t); tg=at(target,L-1)
        rel=(c[0]-sw[0],c[1]-sw[1])
        who=[n for n,h in pl.items() if at(h,t)==c or at(h,t-1)==c]
        tt=at(pl.get(tg,{}),t) if tg else None
        print(p.split('/')[-1],L,"+%d"%(t-L),"face",c,"rel",rel,"target",tg,tt,"raiders on it",who)
