import re
from collections import Counter,defaultdict
A='/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
WINS=[(3268,1463,2261),(3268,2519,3200),(1144,3299,3947),(1144,4480,4918),(1144,5066,5557),(1144,5805,6185),(1144,6347,6732),(1144,7053,7378),(1144,8253,8598),(1144,8725,9101),(1144,9245,9589),(3259,9818,10635),(3259,11079,12082)]
TICK=re.compile(r"^\[(\d+)\]$")
NPCL=re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), id=(crystal_[a-z_]+) \(")
OTHER=re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] (npc|player)=")
SEQ=re.compile(r"^\s+\[sequence\] id=([a-z0-9_]+)")
HIT=re.compile(r"^\s+\[hit\] id=(\d+), value=(\d+)")
SYN=re.compile(r"\[synth_sound\] id=(\d+)(?:, (?:loops=\d+, )?delay=(\d+))?")
SND=re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=")
ev=defaultdict(lambda: defaultdict(list)); base=Counter(); nt=0
for s,a,b in WINS:
    tick=0; cur=None; ticksyn=defaultdict(list); tickev=defaultdict(list)
    for line in open(A+f'idx{s}.txt',errors='replace'):
        m=TICK.match(line)
        if m: tick=int(m.group(1)); cur=None; continue
        if tick<a: continue
        if tick>b: break
        m=NPCL.match(line)
        if m: cur=m.group(3); continue
        if OTHER.match(line): cur=None; continue
        if cur:
            m=SEQ.match(line)
            if m:
                sq=m.group(1)
                k='death' if 'death' in sq else 'attack' if 'attack' in sq else 'defend' if ('defend' in sq or 'block' in sq) else 'seq:'+sq
                tickev[tick].append((cur,k,sq))
            m=HIT.match(line)
            if m: tickev[tick].append((cur,'hit%s'%m.group(1),m.group(2)))
        m=SYN.search(line)
        if m: ticksyn[tick].append('syn%s/%s'%(m.group(1),m.group(2) or 0))
        m=SND.search(line)
        if m: ticksyn[tick].append('area%s'%m.group(1))
    for t in range(a,b+1):
        nt+=1
        for x in set(ticksyn.get(t,[])): base[x]+=1
        for (n,k,v) in tickev.get(t,[]):
            ev[(n.replace('_hm','~'),k)]['_n'].append(1)
            if k in('attack','death') or k.startswith('hit'):
                ev[(n.replace('_hm','~'),k)]['seqs'].append(v)
            for x in set(ticksyn.get(t,[])): ev[(n.replace('_hm','~'),k)][x].append(1)
for key in sorted(ev):
    d=ev[key]; n=len(d['_n'])
    if n<2: continue
    top=sorted(((len(v),x) for x,v in d.items() if x not in('_n','seqs')),reverse=True)
    lift=[(c,x) for c,x in top if c/n>0.5 and c/n > 3*base[x]/nt][:5]
    print(key, 'n=%d'%n, Counter(d['seqs']).most_common(2) if key[1] in('attack','death') else '', ['%s %d/%d'%(x,c,n) for c,x in lift])
