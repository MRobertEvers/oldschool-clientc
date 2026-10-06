import sys,collections
path=sys.argv[1]
rows=[l.rstrip("\n").split("\t") for l in open(path)][1:]
for r in rows:
    serial,tick,kind=r[0],int(r[1]),r[2]
    a=r[3:9]
    if kind in ("npc_anim","projectile","hit_player","npc_retype","npc_spawn","npc_death","map_spotanim","npc_spotanim"):
        print(tick,kind,*a,r[9] if len(r)>9 else "")
