import json,glob,collections,sys
files=sorted(glob.glob('blert_nylo_raw/*.json'))
if len(sys.argv)>1: files=[f for f in files if sys.argv[1] in f]
natural=collections.defaultdict(collections.Counter)  # (big)->lifetime counter
killed=collections.defaultdict(collections.Counter)
wavegaps=collections.defaultdict(list)
splitlag=collections.Counter()
for f in files:
    ev=json.load(open(f))
    spawn={};death={};hp={}; upd=collections.defaultdict(list)
    for e in ev:
        n=e.get('npc')
        if not n: continue
        rid=n['roomId']
        if e['type']==7 and 'nylo' in n: spawn[rid]=e
        elif e['type']==9: death[rid]=e
        elif e['type']==8 and rid in spawn:
            upd[rid].append((e['tick'],n.get('hitpoints',0)))
    # lifetimes
    for rid,s in spawn.items():
        if rid not in death: continue
        ny=s['npc']['nylo']; big=ny['big']
        life=death[rid]['tick']-s['tick']
        # find hp==0 tick
        zero=[t for t,h in upd[rid] if (h>>16)==0 and h!=0 or h==0]
        # hitpoints format: current<<16|base? use >>16
        hz=[t for t,h in upd[rid] if (h>>16)==0]
        if life>=50 and not hz: natural[big][life]+=1
        elif hz:
            killed[big][death[rid]['tick']-hz[0]]+=1
print('natural explosion spawn->despawn event (small/big)',{k:dict(v) for k,v in natural.items()})
print('killed: first hp==0 update -> despawn (big False/True)',{k:dict(v) for k,v in killed.items()})
