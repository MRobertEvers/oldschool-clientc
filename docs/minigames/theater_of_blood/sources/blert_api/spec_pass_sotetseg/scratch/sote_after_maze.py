import json,glob,collections
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"M",8:"B",9:"D"}
c=collections.Counter(); dpos=collections.Counter()
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f))
    att=sorted([(e["tick"],ATT[e["npcAttack"]["attack"]]) for e in ev if e["type"]==10 and e["npcAttack"]["attack"] in ATT])
    ids=sorted([(e["tick"],e["npc"]["id"]) for e in ev if e["type"] in (7,8) and e["npc"]["id"] in (8388,8387,10865,10864,10868,10867)])
    prev=None;act=[]
    for t,i in ids:
        if i in (8388,10865,10868) and prev in (8387,10864,10867): act.append(t)
        prev=i
    for a in act:
        seq=[x for x in att if x[0]>=a]
        if seq: c[seq[0][1]]+=1
        n=0
        for t,k in seq:
            n+=1
            if k=="D": dpos[n]+=1; break
print("type of the first attack after each re-activation (44 mazes, normal and hard):",dict(c),"(M melee, B ball, D death ball)")
print("attack number (1 = the very first) of the first death ball after a re-activation:",dict(sorted(dpos.items())))
