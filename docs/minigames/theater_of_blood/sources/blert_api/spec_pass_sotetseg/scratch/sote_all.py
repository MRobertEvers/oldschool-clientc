import json,glob,collections,os,sys
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"M",8:"B",9:"D"}
for mode in ("m11","m10","m12"):
    files=sorted(glob.glob(D+"/%s_*_13.json"%mode))
    print("=====",mode,len(files),"raids")
    first=[];postD=collections.Counter();bb=collections.Counter();dcount=0;gap_all=collections.Counter()
    proc_hp=[];hp_start=[];  postmaze=[];  maze_ticks=[]
    for f in files:
        ev=json.load(open(f)); name=os.path.basename(f)
        att=sorted([(e["tick"],ATT.get(e["npcAttack"]["attack"],"?"),e["npcAttack"].get("target")) for e in ev if e["type"]==10])
        if not att: continue
        first.append(att[0][0])
        mz=sorted(e["tick"] for e in ev if e["type"]==130)
        # hp of sote
        hpt={}
        for e in ev:
            if e["type"] in (7,8) and "hitpoints" in e["npc"] and e["npc"]["id"] in (8388,8387,10865,10864,10868,10867):
                hpt[e["tick"]]=(e["npc"]["id"],e["npc"]["hitpoints"]>>16,e["npc"]["hitpoints"]&0xffff)
        if hpt: hp_start.append(hpt[min(hpt)][1:])
        for mt in mz:
            near=[t for t in hpt if t<=mt]
            if near: proc_hp.append((mt,)+hpt[max(near)][1:])
        cnt=0
        for i,(t,a,tg) in enumerate(att):
            if a=="B": cnt+=1
            if a=="D":
                dcount+=1; bb[cnt]+=1; cnt=0
                if i+1<len(att):
                    g=att[i+1][0]-t
                    # skip if a maze proc falls inside the gap
                    inside=any(t<=m<=att[i+1][0] for m in mz)
                    postD[(g,"maze" if inside else "no maze")]+=1
        for i in range(len(att)-1):
            t=att[i][0]; g=att[i+1][0]-t
            if not any(t<=m<=att[i+1][0] for m in mz) and att[i][1]!="D": gap_all[g]+=1
        # first attack after each maze: use id change events 8387->8388
        ids=sorted([(e["tick"],e["npc"]["id"]) for e in ev if e["type"] in (7,8) and e["npc"]["id"] in (8388,8387,10865,10864,10868,10867)])
        prev=None
        for t,i in ids:
            if prev in (8387,10864,10867) and i in (8388,10865,10868):
                nxt=[a[0] for a in att if a[0]>=t]
                if nxt: postmaze.append(nxt[0]-t)
            prev=i
    print("first attack",sorted(first))
    print("balls before D",dict(sorted(bb.items())),"D total",dcount)
    print("gap after D (outside maze)",dict(sorted(postD.items(),key=str)))
    print("gaps not spanning maze and not after D",dict(sorted(gap_all.items())))
    print("post-maze first attack delay from reactivation",sorted(postmaze))
    print("sote hp at maze procs (tick,cur,max):",proc_hp[:12])
    print("start hp",hp_start[:6])
