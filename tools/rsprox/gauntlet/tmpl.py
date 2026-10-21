import json
from collections import Counter,defaultdict
R=json.load(open('reveals.json'))
def place(x,z,r,n=16):
    for _ in range(r): x,z = z, n-1-x
    return x,z
def tmpl_cell(tr):
    base = 1920 if tr[0]>=1920 else 1856
    return ((tr[0]-base)//16,(tr[1]-5632)//16),((tr[0]-base)%16//8,(tr[1]-5632)%16//8)
rows=[]
for r in R:
    if len(r['rooms'])!=1: continue
    room=r['rooms'][0]; rx,rz=room%7,room//7
    items=[('L',l[0],tuple(l[2]),l[4],l[3]) for l in r['locs'] if l[1]==[rx,rz]]+[('N',n[0],tuple(n[2]),n[4],n[3]) for n in r['npcs'] if n[1]==[rx,rz]]
    if not items: continue
    cells=Counter(); rots=Counter()
    for k,name,loc,tr,_ in items:
        cell,tq=tmpl_cell(tr); cells[cell]+=1
        rq=(loc[0]//8,loc[1]//8)
        for rot in range(4):
            if place(tq[0],tq[1],rot,2)==rq: rots[rot]+=1
    cell=cells.most_common(1)[0][0]; rot=rots.most_common(1)[0][0]
    ok = rots[rot]==len(items) and len(cells)==1
    tl=[]
    for k,name,loc,tr,extra in items:
        x,z=loc
        for _ in range((4-rot)%4): x,z=place(x,z,1)
        tl.append((k,name.replace('_hm',''),(x,z),extra))
    rows.append(dict(sess=r['sess'],tick=r['tick'],room=(rx,rz),cell=cell,rot=rot,ok=ok,items=tl,hm=any('_hm' in i[1] for i in items)))
json.dump(rows,open('rooms_tmpl.json','w'))
print('rooms',len(rows),'consistent',sum(r['ok'] for r in rows))
bycell=defaultdict(list)
for r in rows: bycell[r['cell']].append(r)
for c in sorted(bycell):
    rs=bycell[c]
    pos=Counter((i[2]) for r in rs for i in r['items'] if i[0]=='L')
    npos=Counter((i[2]) for r in rs for i in r['items'] if i[0]=='N')
    print(c,'n',len(rs),'rots',Counter(r['rot'] for r in rs),'rooms',Counter(r['room'] for r in rs).most_common(3))
    print('   res spots',sorted(pos.items()))
    print('   npc spots',sorted(npos.items()))
