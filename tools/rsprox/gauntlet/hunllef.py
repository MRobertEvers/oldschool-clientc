import re,sys,json
from collections import Counter,defaultdict
A='/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS=[(3268,2261,2352),(1144,3947,4034),(3259,9780,9801),(3259,10635,10967),(3259,12082,12440),
        (3268,3200,3417),(1144,4918,5019),(1144,5557,5667),(1144,6185,6303),(1144,6732,6844),(1144,7378,7453),(1144,8598,8659),(1144,9101,9224),(1144,9589,9688)]
TICK=re.compile(r"^\[(\d+)\]$")
NPCL=re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+), \d+\))?")
SEQ=re.compile(r"^\s+\[sequence\] id=([a-z0-9_]+) \((\d+)\)(?:, delay=(\d+))?")
SPOT=re.compile(r"^\s+\[spotanim\] (?:slot=\d+, )?id=([a-z0-9_]+) \((\d+)\)(.*)")
TRANS=re.compile(r"^\s+\[transformation\] id=([a-z0-9_]+)")
HIT=re.compile(r"^\s+\[hit\] id=(\d+), value=(\d+)")
PROJ=re.compile(r"\[map_projanim_v[12]\] id=([a-z0-9_]+) \(\d+\), (.*)$")
SND=re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=")
SYN=re.compile(r"\[synth_sound\] id=(\d+)(.*)$")
PL=re.compile(r"^\s+\[(idle|walk|run|teleport)\] player=\(index=\d+, name=Z-Kris")
MSG=re.compile(r'\[message_game\] type=[a-z]+, message="(.*)"')
LOC=re.compile(r"\[loc_add_change_v2\] id=(prif_gauntlet_floor_tile_01[a-z_]*) \(\d+\), coord=\((\d+), (\d+)")
VB=re.compile(r"\[varbit\] id=([a-z0-9_]+) \(\d+\), old=(-?\d+), new=(-?\d+)")
VP=re.compile(r"\[varp_small\] id=([a-z0-9_]+) \(\d+\), old=(-?\d+), new=(-?\d+)")
def fight(s,t0,t1):
    ev=[]; tick=0; ctx=None
    for line in open(A+f'idx{s}.txt',errors='replace'):
        m=TICK.match(line)
        if m: tick=int(m.group(1)); ctx=None; continue
        if tick<t0-2: continue
        if tick>t1+8: break
        m=NPCL.match(line)
        if m:
            name=m.group(3)
            ctx=('npc',name,int(m.group(2))) if 'hunllef' in name else None
            if ctx: ev.append((tick,'npc_'+m.group(1),name,m.group(2),(m.group(5),m.group(6)),(m.group(7),m.group(8))))
            continue
        if PL.match(line): ctx=('me',); continue
        if ctx:
            for R,k in ((SEQ,'seq'),(SPOT,'spot'),(TRANS,'trans'),(HIT,'hit')):
                m=R.match(line)
                if m: ev.append((tick,ctx[0]+'_'+k,ctx[1] if ctx[0]=='npc' else 'me',*m.groups())); break
        m=PROJ.search(line)
        if m: ev.append((tick,'proj',m.group(1),m.group(2)))
        m=SND.search(line)
        if m: ev.append((tick,'snd',m.group(1),m.group(2)))
        m=SYN.search(line)
        if m: ev.append((tick,'syn',m.group(1),m.group(2)))
        m=MSG.search(line)
        if m: ev.append((tick,'msg',m.group(1)))
        m=LOC.search(line)
        if m: ev.append((tick,'floor',m.group(1),m.group(2),m.group(3)))
        m=VB.search(line)
        if m and m.group(1) not in ('clock','date_milliseconds_past_minute','date_seconds_past_minute','date_minutes'): ev.append((tick,'varbit',*m.groups()))
        m=VP.search(line)
        if m: ev.append((tick,'varp',*m.groups()))
    return ev
out={f'{s}@{t0}':fight(s,t0,t1) for s,t0,t1 in FIGHTS}
json.dump(out,open('fights.json','w'))
print({k:len(v) for k,v in out.items()})
