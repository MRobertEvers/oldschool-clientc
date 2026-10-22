"""Every hit on a raider while Ezekiel lives, matched to a recorded rule
(ezekiel_plan.md); whatever no rule explains is printed. A hit at t is judged on
the raider's tile at the end of t-1 (`prev`) or t (`now`).

    python3 ez_residual_hits.py <--all-players transcript>... [--all]

Rules: melee = a tfa_wave_dust on the tile at t (§4); magic = his magic01 at
t-2 (§4b); star = S+3 on the star, S+5 on the star or under his 3x3 (§5);
sword = a sword's flight at f, the raider in its 3-wide band, t in f..f+1 (§5);
volley = a finale impact on the tile landing at t (send + delay//30, §8).
Splat types other than damage/block (16 17 12 13 43 44) are counted apart.
"""
import re,sys
from collections import defaultdict,Counter
PL=re.compile(r"^        \[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")
NPC=re.compile(r"^        \[(\w+)\] npc=\(index=(\d+), id=(tfa_ezekiel|tfa_ezekiel_sword) \((\d+)\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")
DMG={16,17,12,13,43,44}
show_all="--all" in sys.argv
tot=Counter(); dots=Counter(); unexpl=[]
for p in [a for a in sys.argv[1:] if not a.startswith("--")]:
    name=p.split('/')[-1]
    tick=0; cur=None; sect=""; curnpc=None
    tiles={}; hist={}; hits=[]
    boss={}; lives=[]; alive=None
    specs=[]; star=defaultdict(set); dust=defaultdict(set); magic=set()
    flights=[]; impacts=defaultdict(set); seqs=defaultdict(list)
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m:
            hist[tick]=dict(tiles); tick=int(m.group(1)); cur=None; curnpc=None; continue
        if line.startswith("    [") : sect=line.strip(); cur=None; curnpc=None
        m=PL.match(line)
        if m:
            cur=m.group(2); curnpc=None
            x,z=(m.group(5),m.group(6)) if m.group(5) else (m.group(3),m.group(4))
            tiles[cur]=(int(x),int(z)); continue
        m=NPC.match(line)
        if m:
            cur=None; kind,idx,nm=m.group(1),m.group(2),m.group(3)
            c=(int(m.group(5)),int(m.group(6))); n=(int(m.group(7)),int(m.group(8))) if m.group(7) else c
            curnpc=nm
            if nm=="tfa_ezekiel":
                boss[tick]=n
                if kind=="add": alive=tick
                if kind=="del" and alive is not None: lives.append((alive,tick)); alive=None
            elif kind=="teleport" and abs(n[0]-c[0])+abs(n[1]-c[1])>=10:
                flights.append((tick,c,n))
            continue
        if line.startswith("        [") and "npc=" in line: cur=None; curnpc="other"
        m=re.search(r"\[hit\] id=(\d+), value=(\d+)",line)
        if m and cur and sect.startswith("[player_info]"): hits.append((tick,cur,int(m[1]),int(m[2])))
        if curnpc=="tfa_ezekiel" and "[sequence]" in line:
            s=line.split("id=")[1].split()[0]; seqs[tick].append(s)
            if s=="npc_ezekiel_special01": specs.append(tick)
            if s=="npc_ezekiel_magic01": magic.add(tick)
        m=re.search(r"\[map_anim\] id=(\w+) \(\d+\), delay=(\d+), coord=\((\d+), (\d+)",line)
        if m:
            a,d,t2=m[1],int(m[2]),(int(m[3]),int(m[4]))
            if a=="vfx_ezekiel_star_explosion_spawn01": star[tick].add(t2)
            elif a=="tfa_wave_dust": dust[tick].add(t2)
            elif a.startswith("vfx_ezekiel_finale_impact"): impacts[tick+d//30].add(t2)
    hist[tick]=dict(tiles)
    if alive is not None: lives.append((alive,tick))
    def at(d,t):
        b=None
        for k in sorted(d):
            if k<=t: b=d[k]
            else: break
        return b
    def under(tl,b): return b and 0<=tl[0]-b[0]<=2 and 0<=tl[1]-b[1]<=2
    def band(tl,c,n):
        if c[0]==n[0]: return c[0]<=tl[0]<=c[0]+2      # a column flies N/S
        return c[1]<=tl[1]<=c[1]+2                       # a row flies E/W
    for t,who,i,v in hits:
        if not any(a<=t<=b for a,b in lives): continue
        prev=hist.get(t-1,{}).get(who); now=hist.get(t,{}).get(who)
        if prev is None: continue
        if not (4050<=prev[0]<=4085 and 5520<=prev[1]<=5550): continue
        if i not in DMG: dots[(i,)]+=1; continue
        why=[]
        if prev in dust[t] or now in dust[t]: why.append("melee")
        if t-2 in magic: why.append("magic")
        for S in specs:
            b=at(boss,S)
            if t==S+3 and prev in star[S]: why.append("star1")
            if t==S+5 and (prev in star[S] or under(prev,b)): why.append("star2")
        for f,c,n in flights:
            if t in (f,f+1) and band(prev,c,n): why.append(f"sword+{t-f}")
        if prev in impacts[t] or now in impacts[t]: why.append("volley")
        key="+".join(sorted(set(w.split("+")[0] for w in why))) or "UNEXPLAINED"
        tot[key]+=1
        if not why or show_all:
            b=at(boss,t)
            near=" ".join(f"{k-t:+d}:{','.join(seqs[k])}" for k in range(t-8,t+1) if seqs.get(k))
            fl=" ".join(f"fly{f-t:+d}" for f,c,n in flights if -16<=f-t<=2)
            sp=" ".join(f"S{S-t:+d}" for S in specs if -16<=S-t<=0)
            unexpl.append(f"{name} [{t}] {who} id={i} v={v} prev={prev} now={now} boss={b} | {near} | {sp} {fl}")
print("\n".join(unexpl))
print()
for k,v in tot.most_common(): print(f"{v:5d} {k}")
print("non-damage splat types (counted apart):",dict(dots))
