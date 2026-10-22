"""The star's two beats (ezekiel_plan.md §5): per special S and raider, the tile at
the end of S+2 and S+4 (star / under his 3x3 / off) against the hits at S+3 and S+5.

    python3 ez_beats.py <--all-players transcript>...   | grep ROW
"""
import re,sys
from collections import defaultdict
PL=re.compile(r"^        \[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")
import sys
for p in sys.argv[1:]:
    tick=0; cur=None; sect=None
    tiles={}            # name -> tile now
    hist=defaultdict(dict)  # tick -> name -> tile at end of tick
    hits=defaultdict(list)  # tick -> (name,id,val)
    boss={}; specs=[]; star=defaultdict(dict); npcseq=defaultdict(list)
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m:
            hist[tick]=dict(tiles); tick=int(m.group(1)); cur=None; continue
        if line.startswith("    [") and not line.startswith("     "):
            sect=line.strip(); cur=None
        m=PL.match(line)
        if m:
            cur=m.group(2); x,z=(m.group(5),m.group(6)) if m.group(5) else (m.group(3),m.group(4))
            tiles[cur]=(int(x),int(z)); continue
        if line.startswith("        [") and "npc=" in line: cur=None
        m=re.search(r"\[hit\] id=(\d+), value=(\d+)",line)
        if m and cur and sect.startswith("[player_info]"): hits[tick].append((cur,int(m[1]),int(m[2])))
        m=re.search(r"npc=\(index=\d+, id=tfa_ezekiel \(16652\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m: boss[tick]=(int(m.group(3) or m.group(1)),int(m.group(4) or m.group(2)))
        if "npc_ezekiel_special01" in line: specs.append(tick)
        if "npc_ezekiel_" in line and "[sequence]" in line: npcseq[tick].append(line.split("id=")[1].split()[0])
        m=re.search(r"vfx_ezekiel_star_explosion_spawn01 \(4170\), delay=(\d+), coord=\((\d+), (\d+)",line)
        if m: star[tick][(int(m[2]),int(m[3]))]=int(m[1])
    hist[tick]=dict(tiles)

    def bossat(t):
        b=None
        for k in sorted(boss):
            if k<=t: b=boss[k]
        return b
    for S in specs:
        st=star.get(S,{})
        if not st: continue
        names=set(k for d in range(0,8) for k in hist[S+d] if hist[S+d].get(k))
        for n in sorted(names):
            for beat,judge in ((3,2),(5,4)):
                tl=hist[S+judge].get(n)
                if not tl: continue
                b=bossat(S+judge)
                under = b and 0<=tl[0]-b[0]<=2 and 0<=tl[1]-b[1]<=2
                cls="star" if tl in st else ("under" if under else "off")
                hs=[v for (nn,i,v) in hits[S+beat] if nn==n and i in (16,17,12,13)]
                print("ROW",p.split('/')[-1],S,n.replace(" ","_"),beat,cls,b==bossat(S),hs)
