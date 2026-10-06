import json,glob,collections,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
print("== MELEE_DELAY: ticks from a Sotetseg melee attack (TOB_SOTE_MELEE) to the recorder's first hitpoints drop")
meleed=collections.Counter()
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f))
    hp={};rec=None
    for e in ev:
        if e["type"]==4 and "hitpoints" in e["player"]: hp[e["tick"]]=e["player"]["hitpoints"]>>16; rec=e["player"]["name"]
    for e in ev:
        if e["type"]==10 and e["npcAttack"]["attack"]==7 and e["npcAttack"].get("target")==rec:
            t=e["tick"]
            for dt in range(0,4):
                if (t+dt) in hp and (t+dt-1) in hp and hp[t+dt]<hp[t+dt-1]:
                    meleed[dt]+=1; break
            else: meleed["no drop (prayed/blocked/missed)"]+=1
print(dict(meleed))
print()
print("== RAG_FORMULA: recorder hitpoints drop inside a maze window vs 15 + floor(hp_before*67/1000)  (exact matches marked =)")
n=ex=0
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f)); name=os.path.basename(f)[:14]
    procs=[e["tick"] for e in ev if e["type"]==130]
    hp={}
    for e in ev:
        if e["type"]==4 and "hitpoints" in e["player"]: hp[e["tick"]]=e["player"]["hitpoints"]>>16
    atk=set(e["tick"] for e in ev if e["type"]==10)
    for t in sorted(hp):
        if t-1 in hp and 14<=hp[t-1]-hp[t]<=21 and any(0<=t-p<=60 for p in procs):
            b=hp[t-1];d=b-hp[t];pred=15+b*67//1000
            n+=1; ex+=(d==pred)
            print(name,"tick",t,"hp before",b,"drop",d,"predicted",pred,"=" if d==pred else "")
print("rows",n,"exact",ex)
print()
print("== CYCLE_PHASE: reactivation tick (npc idle->active) mod 4, both mazes of one raid")
same=diff=0
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f))
    ids=sorted([(e["tick"],e["npc"]["id"]) for e in ev if e["type"] in (7,8) and e["npc"]["id"] in (8388,8387,10865,10864,10868,10867)])
    prev=None;act=[]
    for t,i in ids:
        if i in (8388,10865,10868) and prev in (8387,10864,10867): act.append(t)
        prev=i
    if len(act)==2:
        r=[a%4 for a in act]; 
        if r[0]==r[1]: same+=1
        else:
            diff+=1; print("differs:",os.path.basename(f)[:14],act,r,"gap mod 4 =",(act[1]-act[0])%4)
print("same residue",same,"differ",diff)
print()
print("== ATTACKS_DURING_MAZE and FIRST_MOVE: see sote_misc / sote_stall outputs")
