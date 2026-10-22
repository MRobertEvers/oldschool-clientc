"""Every gong's beats against the rule (reach, Manhattan delays from his drum on his pre-step tile, the cart and its shadow).
    python3 bk_beatcheck.py <all-players transcript | ticklog.tsv>...
"""
import re,sys
from collections import defaultdict
CART=(4190,5598)
def cart(t): return CART[0]<=t[0]<=CART[0]+2 and CART[1]<=t[1]<=CART[1]+2
def bd(sw,c,n=4):
    dx=max(0,sw[0]-c[0],c[0]-(sw[0]+n-1)); dz=max(0,sw[1]-c[1],c[1]-(sw[1]+n-1)); return max(dx,dz)
def los(a,b):
    # sample the segment centre-to-centre; blocked if it crosses a cart tile
    n=max(abs(b[0]-a[0]),abs(b[1]-a[1]))*4
    for i in range(1,n):
        x=a[0]+(b[0]-a[0])*i/n; z=a[1]+(b[1]-a[1])*i/n
        if cart((round(x),round(z))): return False
    return True
def check(gongs,label):
    bad=0; tot=0; miss_unexplained=0; delaybad=0
    for T,(sw,reach,dust) in sorted(gongs.items()):
        if not dust: continue
        tot+=1
        drum=(sw[0]+2,sw[1]+2)
        for t,d in dust.items():
            if d!=abs(t[0]-drum[0])+abs(t[1]-drum[1]): delaybad+=1
        got=set(dust); mx=max(bd(sw,t) for t in got)
        exp={(sw[0]+dx,sw[1]+dz) for dx in range(-reach,4+reach) for dz in range(-reach,4+reach)}
        exp={t for t in exp if 0<bd(sw,t)<=reach and not cart(t)}
        missing=exp-got; extra=got-exp
        unexpl=[t for t in missing if los(drum,t)]
        # tiles beyond the arena are missing legitimately
        unexpl=[t for t in unexpl if 4184<=t[0]<=4198 and 5591<=t[1]<=5607]
        if extra or unexpl:
            bad+=1
            if bad<=6: print(label,T,"sw",sw,"reach",reach,"maxring",mx,"extra",sorted((a-sw[0],b-sw[1]) for a,b in extra)[:5],"unexplained-missing",sorted((a-sw[0],b-sw[1]) for a,b in unexpl)[:5])
    print(label,"gongs",tot,"off",bad,"delay-mismatch tiles",delaybad)
def at(d,t):
    ks=[k for k in d if k<=t]; return d[max(ks)] if ks else None
def build(boss,pl,target_of,gong_ticks,dust,skip):
    g={}
    for T in gong_ticks:
        if T in skip: continue
        sw=at(boss,T-1) or at(boss,T); sw0=sw
        tg=at(target_of,T-1)
        p=at(pl.get(tg,{}),T-1) if tg else None
        reach=max(1,bd(sw0,p)) if p else 1
        g[T]=(sw,reach,dust.get(T,{}))
        g[T+0.5]=(at(boss,T+1) or sw,1,dust.get(T+2,{}))
    return g
def rec(path):
    t=0; boss={}; pl=defaultdict(dict); target={}; gongs=[]; dust=defaultdict(dict); cur=None; skip=set()
    for line in open(path,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            pl[m[2]][t]=(int(m[5] or m[3]),int(m[6] or m[4])); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            cur=m[2]
            if cur=="tfa_bork": boss[t]=(int(m[5] or m[3]),int(m[6] or m[4]))
            continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_bork":
            mm=re.search(r"\[facing\] player=\(index=\d+, name=([^,]+),",line)
            if mm: target[t]=mm[1]
            if "gong_melee01" in line: gongs.append(t)
            if "stun01" in line or "stomp" in line: skip.add(t)
        mm=re.search(r"\[map_anim\] id=tfa_wave_dust_short \(\d+\)(?:, delay=(\d+))?, coord=\((\d+), (\d+)",line)
        if mm: dust[t][(int(mm[2]),int(mm[3]))]=int(mm[1] or 0)
    return build(boss,pl,target,gongs,dust,skip)
def ours(path):
    boss={}; pl={"me":{}}; target={}; gongs=[]; dust=defaultdict(dict); slot=None
    conv=lambda c:(((c>>14)&16383)-2240,(c&16383)+5440)
    for line in open(path):
        v=line.rstrip("\n").split("\t")
        if len(v)<9 or not v[1].isdigit(): continue
        t=int(v[1]); k=v[2]
        if k=="npc_spawn" and v[4]=="16640": slot=v[3]; boss[t]=conv(int(v[5]))
        if k=="npc_tile" and v[3]==slot: boss[t]=(int(v[4])-2240,int(v[5])+5440)
        if k=="player_tile": pl["me"][t]=(int(v[4])-2240,int(v[5])+5440); target[0]="me"
        if k=="npc_anim" and v[4]=="16640" and v[5]=="14542": gongs.append(t)
        if k=="map_spotanim" and v[4]=="4395": dust[t][conv(int(v[3]))]=int(v[6])
    return build(boss,pl,target,gongs,dust,set())
for p in sys.argv[1:]:
    check(ours(p) if p.endswith(".tsv") else rec(p), p.split("/")[-1])
