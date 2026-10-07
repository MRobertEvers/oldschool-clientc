import json,glob,collections,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"melee",8:"ball",9:"death"}
# 1 attacks during maze
during=0; total=0
# 2 melee hit delay on recorder
meleed=collections.Counter()
# 3 prayer set after hits
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f)); name=os.path.basename(f)[:20]
    proc=sorted(e["tick"] for e in ev if e["type"]==130)
    ids=sorted([(e["tick"],e["npc"]["id"]) for e in ev if e["type"] in (7,8) and e["npc"]["id"] in (8388,8387,10865,10864,10868,10867)])
    idle=[];act=[];prev=None
    for t,i in ids:
        if i in (8387,10864,10867) and prev not in (8387,10864,10867): idle.append(t)
        if i in (8388,10865,10868) and prev in (8387,10864,10867): act.append(t)
        prev=i
    att=sorted([(e["tick"],e["npcAttack"]["attack"],e["npcAttack"].get("target")) for e in ev if e["type"]==10])
    for t,a,tg in att:
        total+=1
        if any(i<t<=a2 for i,a2 in zip(idle,act)) : during+=1
    hp={};pset={};rec=None
    for e in ev:
        if e["type"]==4 and "hitpoints" in e["player"]:
            hp[e["tick"]]=e["player"]["hitpoints"]>>16; rec=e["player"]["name"]
    for e in ev:
        if e["type"]==4 and e["player"]["name"]==rec: pset[e["tick"]]=e["player"]["prayerSet"]
    for t,a,tg in att:
        if tg==rec and a==7:
            for dt in range(0,4):
                if (t+dt) in hp and (t+dt-1) in hp and hp[t+dt]<hp[t+dt-1]:
                    meleed[dt]+=1; break
            else: meleed["none"]+=1
print("attacks total",total,"strictly inside idle->active windows",during)
print("melee on recorder: ticks until first hp drop",dict(meleed))
# prayer set values observed
c=collections.Counter()
for f in sorted(glob.glob(D+"/m1*_13.json"))[:3]:
    ev=json.load(open(f))
    for e in ev:
        if e["type"]==4: c[e["player"]["prayerSet"]]+=1
print("prayerSet values",c.most_common(8))
