"""Every stun: its index in the fight, the drum's rate, whether the stun spot shows.
    python3 bk_stunspot.py <transcript>...
"""
import re,sys
for p in sys.argv[1:]:
    t=0; cur=None; fight=0; n=0; rows={}; spots=set(); rate=7; last_engage=None
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        if "id=12769," in line: fight+=1; n=0; rate=7
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) ",line)
        if m: cur=m[2]; continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_bork":
            if "Bork get into it" in line: rate=6
            if "Bork make fast drum" in line: rate=5
            if "Bork maximum crush" in line: rate=4
            if "stun01 (" in line and "sequence" in line:
                n+=1; rows[t]=[fight,n,rate,False]
            if "vfx_bork_14_cycle_stun_spotanim01" in line: spots.add(t)
    for t in rows: rows[t][3]= t in spots
    for t,(f,k,r,sp) in sorted(rows.items()):
        print(p.split('/')[-1],t,"fight",f,"stun#",k,"rate",r,"spot",sp)
