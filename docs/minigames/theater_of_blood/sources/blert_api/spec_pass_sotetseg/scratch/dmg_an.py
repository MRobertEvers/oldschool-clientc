import sys
W=sys.argv[1]
rows=[l.rstrip("\n").split("\t") for l in open(W+"/build/quest_gate/spec_sotetseg_dmg_off/ticklog.tsv")][1:]
anim={}
for r in rows:
    if r[2]=="npc_anim" and r[5] in ("8138","8139"): anim[int(r[1])]=r[5]
mel=[];oth=[]
for r in rows:
    if r[2]=="hit_player" and r[8]!="-1":
        t=int(r[1]); dmg=int(r[5])
        (mel if anim.get(t-2)=="8138" else oth).append(dmg)
print("melee hits",sorted(mel))
print("ball hits",sorted(oth))
