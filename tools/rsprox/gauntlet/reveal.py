import re,sys,json
from collections import Counter,defaultdict
TICK=re.compile(r"^\[(\d+)\]$")
ME=re.compile(r"\[(?:idle|walk|run|teleport|crawl)\] player=\(index=\d+, name=Z-Kris, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
FOUND=re.compile(r"\[varbit\] id=gauntlet_room_(\d+)_found \(\d+\), old=0, new=1")
START=re.compile(r"\[varbit\] id=gauntlet_start \(\d+\), old=-?\d+, new=(\d+)")
ENTER='message="You enter the Gauntlet."'
LOC=re.compile(r"\[loc_add_change_v[12]\] id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), \d+\), shape=(\d+), rotation=(\d)")
NPC=re.compile(r"\[add\] npc=\(index=\d+, id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), \d+\)\)(?:, creationcycle=\d+)?, spawnangle=([a-z-]+)")
def run(tr,raw,sess):
    out=[]; tick=0; origin=None; pending_start=None; cur=None; startroom=None
    for lt,lr in zip(open(tr,errors='replace'),open(raw,errors='replace')):
        m=TICK.match(lt)
        if m:
            tick=int(m.group(1))
            if cur: out.append(cur); cur=None
            continue
        m=START.search(lt)
        if m: startroom=int(m.group(1)); origin=None
        m=ME.search(lr)
        if m and startroom is not None and origin is None:
            c=m.groups()
            if c[3]:
                x,z=int(c[3]),int(c[4])
                if x>6400:
                    sx,sz=startroom%7,startroom//7
                    origin=((x-16*sx)//16*16,(z-16*sz)//16*16)
        m=FOUND.search(lt)
        if m:
            r=int(m.group(1))
            if cur and cur['tick']==tick: cur['rooms'].append(r)
            else:
                if cur: out.append(cur)
                cur=dict(sess=sess,tick=tick,rooms=[r],locs=[],npcs=[])
            continue
        if cur is None or origin is None: continue
        m=LOC.search(lt); mr=LOC.search(lr)
        if m and mr:
            rx,rz=int(mr.group(3)),int(mr.group(4))
            room=((rx-origin[0])//16,(rz-origin[1])//16)
            cur['locs'].append((m.group(1),room,((rx-origin[0])%16,(rz-origin[1])%16),int(m.group(6)),(int(m.group(3)),int(m.group(4)))))
        m=NPC.search(lt); mr=NPC.search(lr)
        if m and mr:
            rx,rz=int(mr.group(3)),int(mr.group(4))
            room=((rx-origin[0])//16,(rz-origin[1])//16)
            cur['npcs'].append((m.group(1),room,((rx-origin[0])%16,(rz-origin[1])%16),m.group(5),(int(m.group(3)),int(m.group(4)))))
    return out
res=[]
for s in sys.argv[1:]:
    tr=f"{__import__('os').path.expanduser('~')}/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx{s}.txt"
    raw=tr.replace('home-default','home-raw')
    res+=run(tr,raw,s)
json.dump(res,open('reveals.json','w'))
print(len(res))
