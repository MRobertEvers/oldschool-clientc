#!/usr/bin/env python3
"""Verzik Vitur: every way a raider is THROWN, read from RSProx transcripts made
with every player shown (PLAYER_INFO_LOCAL_PLAYER_ONLY=false; the recipe is in
tools/cox/rsprox_olm_party.py's docstring - the shared local-player transcripts
hold only the recorder, who was never under a pillar or under her in P2).

    python3 tools/rsprox_verzik_throws.py <section> <transcript.txt> ...

Sections:
  bounce   P2 slam/stomp: every human_troll_flyback 1157 - her swing offset, the
           exactmove, the hit, the first own step after it (the stun)
  pillar   every collapsing pillar (8377 + 8052) against every raider within 5
           of its centre, T..T+6: who was thrown (1114), where, the hit
  web      every P3 webspin start (8127) against every raider within 6 of her
           middle: who was thrown (1114) and where
  walkover every P3 step that put her 7x7 onto a raider, and what happened to
           them over the next 3 ticks (nothing, in 53 steps)

Findings: docs/rsprox-analysis/tob/verzik.md, "Getting thrown".
"""
import collections
import re
import sys

TICK=re.compile(r"^\[(\d+)\]\s*$")
ENT=re.compile(r"^ {8}\[([a-z_]+)\] (player|npc)=\(index=(\d+), (?:name=([^,]*)|id=(?:([a-z0-9_]+) \()?(\d+)\)?|[^,]*?\(id=(\d+)\))"
               r".*?coord=\((\d+), (\d+), (\d+)\)[^)]*\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
EXT=re.compile(r"^ {12}\[([a-z_]+)\] ?(.*)$")
PKT=re.compile(r"^ {4}\[([a-z_0-9]+)\] ?(.*)$")
IDN=re.compile(r"id=(?:([a-z0-9_]+) \()?(-?\d+)\)?")
VZ=set(range(8369,8376))
def idof(s):
    m=IDN.search(s); return int(m.group(2)) if m else None
def parse(path):
    ticks=[]; cur=None; ent=None
    for l in open(path,errors='replace'):
        l=l.rstrip('\n')
        m=TICK.match(l)
        if m:
            cur={'t':int(m.group(1)),'p':{},'n':{},'pk':[]}; ticks.append(cur); ent=None; continue
        if cur is None: continue
        m=ENT.match(l)
        if m:
            kind,isp,idx=m.group(1),m.group(2),int(m.group(3))
            c=(int(m.group(8)),int(m.group(9)),int(m.group(10)))
            nc=(int(m.group(11)),int(m.group(12)),int(m.group(13))) if m.group(11) else None
            e={'kind':kind,'idx':idx,'name':m.group(4),'id':int(m.group(6) or m.group(7)) if (m.group(6) or m.group(7)) else None,'c':c,'nc':nc,'x':[]}
            (cur['p'] if isp=='player' else cur['n'])[idx]=e; ent=e; continue
        m=EXT.match(l)
        if m and ent is not None:
            ent['x'].append((m.group(1),m.group(2))); continue
        m=PKT.match(l)
        if m:
            ent=None; cur['pk'].append((m.group(1),m.group(2)))
    return ticks


def bounce(paths):
    HIT=re.compile(r"id=(\d+), value=(\d+)")
    first=collections.Counter(); swing=collections.Counter(); dmg=[]
    for path in paths:
        T=parse(path); f=path.split('/')[-1]
        for i,tk in enumerate(T):
            for e in tk['p'].values():
                seqs=[idof(v) for k,v in e['x'] if k=='sequence']
                if 1157 not in seqs: continue
                # her swing: latest 8116 before
                sw=None
                for j in range(i,max(0,i-4),-1):
                    for n in T[j]['n'].values():
                        if n['id'] in VZ and any(k=='sequence' and idof(v)==8116 for k,v in n['x']): sw=T[j]['t']-tk['t']; break
                    if sw is not None: break
                swing[sw]+=1
                h=[HIT.search(v).groups() for k,v in e['x'] if k=='hit']
                dmg.append(h)
                # first own movement / action after
                mv=None; act=None; rows=[]
                for k in range(i+1,min(len(T),i+12)):
                    for p in T[k]['p'].values():
                        if p['name']!=e['name']: continue
                        s=[idof(v) for kk,v in p['x'] if kk=='sequence']
                        if mv is None and p['kind'] in ('walk','run'): mv=T[k]['t']-tk['t']
                        if act is None and s and s[0] not in (829,424,-1,1157): act=(T[k]['t']-tk['t'],s[0])
                msgs=[v for k,v in tk['pk'] if k=='message_game']
                first[mv]+=1
                em=[v for k,v in e['x'] if k=='exactmove']
                print(f,tk['t'],e['name'],'swing',sw,'move+',mv,'act',act,'hits',h,em[0].split('delay2=')[1] if em else '-')
    print('swing offsets',swing); print('first move',sorted(first.items(),key=lambda x:(x[0] is None,x[0])))


def pillar(paths):
    HIT=re.compile(r"id=(\d+), value=(\d+)")
    for path in paths:
        T=parse(path); f=path.split('/')[-1]
        pos={}   # player name -> coord at start of tick
        for i,tk in enumerate(T):
            # collapse: 8377 add with seq 8052
            cols=[e for e in tk['n'].values() if e['id']==8377 and e['kind']=='add']
            start={}
            for e in tk['p'].values():
                if e['kind']=='del': continue
                start[e['name']]=e['c']
            snap=dict(pos); snap.update(start)
            for e in tk['p'].values():
                if e['kind']!='del': pos[e['name']]=e['nc'] or e['c']
            for c in cols:
                cx,cz=c['c'][0]+2,c['c'][1]+2
                vzform=None
                for j in range(i,max(0,i-30),-1):
                    for e in T[j]['n'].values():
                        if e['id'] in VZ: vzform=e['id']; break
                    if vzform: break
                for name,p in snap.items():
                    dx,dz=p[0]-cx,p[1]-cz
                    if max(abs(dx),abs(dz))>5: continue
                    # follow this player T..T+6
                    rows=[]
                    for k in range(i,min(len(T),i+7)):
                        tk2=T[k]
                        for e in tk2['p'].values():
                            if e['name']!=name: continue
                            bits=[]
                            if e['kind'] in('walk','run','teleport'): bits.append(f"{e['kind']}{(e['nc'][0]-e['c'][0],e['nc'][1]-e['c'][1])}")
                            for kk,v in e['x']:
                                if kk=='sequence': bits.append(f"seq{idof(v)}")
                                if kk=='spotanim': bits.append(f"spot{idof(v)}")
                                if kk=='hit': bits.append("hit"+'/'.join(HIT.search(v).groups()))
                                if kk=='exactmove': bits.append("EM["+v.split('angle=')[1]+',d2='+re.search(r'delay2=(\d+)',v).group(1)+']')
                            if bits: rows.append(f"+{k-i}:"+' '.join(bits))
                    print(f, tk['t'], 'vz',vzform, 'pillar',(cx,cz), name, 'off',(dx,dz), ' | '.join(rows))


def web(paths):
    HIT=re.compile(r"id=(\d+), value=(\d+)")
    for path in paths:
        T=parse(path); f=path.split('/')[-1]
        pos={}
        for i,tk in enumerate(T):
            start=dict(pos)
            for e in tk['p'].values():
                if e['kind']!='del': start[e['name']]=e['c']
            for e in tk['p'].values():
                if e['kind']!='del': pos[e['name']]=e['nc'] or e['c']
            for n in tk['n'].values():
                if n['id']!=8374: continue
                if not any(k=='sequence' and idof(v)==8127 for k,v in n['x']): continue
                sw=n['nc'] or n['c']; cx,cz=sw[0]+3,sw[1]+3
                print(f,tk['t'],'webspin at SW',sw,n['kind'])
                for name,p in sorted(start.items()):
                    d=(p[0]-cx,p[1]-cz)
                    if max(abs(d[0]),abs(d[1]))>6: continue
                    rows=[]
                    for k in range(i,min(len(T),i+8)):
                        for e in T[k]['p'].values():
                            if e['name']!=name: continue
                            bits=[]
                            if e['kind'] in('walk','run','teleport'): bits.append(f"{e['kind']}{(e['nc'][0]-e['c'][0],e['nc'][1]-e['c'][1])}")
                            for kk,v in e['x']:
                                if kk=='sequence': bits.append(f"seq{idof(v)}")
                                if kk=='spotanim': bits.append(f"spot{idof(v)}")
                                if kk=='hit': bits.append("hit"+'/'.join(HIT.search(v).groups()))
                                if kk=='exactmove': bits.append("EM[d1="+re.search(r'delay1=(\d+)',v).group(1)+",d2="+re.search(r'delay2=(\d+)',v).group(1)+",a="+v.split('angle=')[1]+"]")
                            if bits: rows.append(f"+{k-i}:"+' '.join(bits))
                    print('    ',name,'fromCentre',d,' | '.join(rows)[:230])


def walkover(paths):
    HIT=re.compile(r"id=(\d+), value=(\d+)")
    P3={8374}
    def inside(p,sw,n=7): return sw[0]<=p[0]<sw[0]+n and sw[1]<=p[1]<sw[1]+n
    summ=collections.Counter()
    for path in paths:
        T=parse(path); f=path.split('/')[-1]
        pos={}
        for i,tk in enumerate(T):
            start=dict(pos)
            for e in tk['p'].values():
                if e['kind']!='del': start[e['name']]=e['c']
            for e in tk['p'].values():
                if e['kind']!='del': pos[e['name']]=e['nc'] or e['c']
                else: pos.pop(e['name'],None)
            for n in tk['n'].values():
                if n['id'] not in P3 or not n['nc']: continue
                old,new=n['c'],n['nc']
                seqs=[idof(v) for k,v in n['x'] if k=='sequence']
                for name,p in start.items():
                    if not inside(p,new): continue
                    was=inside(p,old)
                    rows=[]
                    for k in range(i,min(len(T),i+4)):
                        for e in T[k]['p'].values():
                            if e['name']!=name: continue
                            bits=[]
                            if e['kind'] in('walk','run','teleport'): bits.append(f"{e['kind']}{(e['nc'][0]-e['c'][0],e['nc'][1]-e['c'][1])}")
                            for kk,v in e['x']:
                                if kk=='sequence': bits.append(f"seq{idof(v)}")
                                if kk=='spotanim': bits.append(f"spot{idof(v)}")
                                if kk=='hit': bits.append("hit"+'/'.join(HIT.search(v).groups()))
                                if kk=='exactmove': bits.append("EM["+v+"]")
                            if bits: rows.append(f"+{k-i}:"+' '.join(bits))
                    msgs=[v for k,v in tk['pk'] if k=='message_game']
                    summ['already' if was else 'stepped_onto']+=1
                    print(f,tk['t'],n['kind'],old,'->',new,'vzseq',seqs,name,'at',(p[0]-new[0],p[1]-new[1]),'already' if was else 'ONTO',' | '.join(rows), msgs[:2])
    print(summ)


SECTIONS = {"bounce": bounce, "pillar": pillar, "web": web, "walkover": walkover}

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in SECTIONS:
        sys.exit(__doc__)
    SECTIONS[sys.argv[1]](sys.argv[2:])
