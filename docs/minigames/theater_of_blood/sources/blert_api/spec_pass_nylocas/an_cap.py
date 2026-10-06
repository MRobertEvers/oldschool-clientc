import sys,collections,json
W=json.load(open('docs/minigames/theater_of_blood/sources/blert_nylocas-waves.json'))
stall={w['num']:w['naturalStall'] for w in W}
hard=len(sys.argv)>2 and sys.argv[2]=='hard'
rows=[]
for l in open(sys.argv[1]):
    p=l.rstrip('\n').split('\t')
    if len(p)<9 or p[0].startswith('ticklog'): continue
    try: rows.append((int(p[0]),int(p[1]),p[2],[int(x) for x in p[3:9]]))
    except: pass
rows.sort()
nyl=lambda t:(8342<=t<=8353) or (10774<=t<=10785) or (10791<=t<=10802)
PR=(10803,10804,10805,10806)
ev=[]
for s,t,k,a in rows:
    if k=='npc_spawn' and (nyl(a[1]) or a[1] in PR): ev.append((t,'s',a[0],3 if a[1] in PR else 1))
    if k=='npc_free' and (nyl(a[1]) or a[1] in PR): ev.append((t,'f',a[0],0))
start=[r[1] for r in rows if r[2]=='npc_spawn' and r[3][1] in (8358,10790,10811)][0]
spawn_ticks=sorted({t for t,k,_,_ in ev if k=='s' and not any(True for _ in [0]) } )
# wave ticks
wt=collections.OrderedDict()
for t,k,sl,w in ev:
    pass
# waves = spawn ticks of lane-spawns; group distinct ticks of 's' excluding splits (none: no kills) and prince-only
ticks=sorted({t for t,k,_,_ in ev if k=='s'})
def alive(t,incl_same_tick_free):
    st={}
    for tt,k,sl,w in sorted(ev):
        if tt>t: break
        if k=='f' and (tt<t or incl_same_tick_free): st.pop(sl,None)
        elif k=='s' and tt<t: st[sl]=w
    return sum(st.values())
cap=lambda wave: (15 if hard else 12) if wave<20 else 24
stall_alive=[];spawn_alive=[]
prev=None
for i,t in enumerate(ticks):
    wave=i+1
    if i==0: prev=t; continue
    # due = prev + natural stall of wave i (wave out = i)
    st=stall[i]
    if hard and i in (10,20,30): st=16
    due=prev+st
    # stall ticks: due, due+4,... < t
    x=due
    while x<t:
        stall_alive.append((i,x-start,alive(x,True),alive(x,False),cap(i))); x+=4
    spawn_alive.append((i+1,t-start,alive(t,True),alive(t,False),cap(i)))
    prev=t
print('stall ticks (wave out,rel,alive_after_free,alive_before_free,cap):',len(stall_alive))
print(' min margin over cap (after-free):',min(a[2]-a[4] for a in stall_alive),' (before-free):',min(a[3]-a[4] for a in stall_alive))
print(' stalls where alive<cap:',[a for a in stall_alive if a[3]<a[4]][:5])
print('spawn ticks alive>=cap (after-free count):',[a for a in spawn_alive if a[2]>=a[4]][:6],'(before-free):',[a for a in spawn_alive if a[3]>=a[4]][:6])
print('stalls by pre/post 20:', collections.Counter((a[0]<20) for a in stall_alive), 'min alive pre', min([a[3] for a in stall_alive if a[0]<20] or [None]),'post',min([a[3] for a in stall_alive if a[0]>=20] or [None]))
