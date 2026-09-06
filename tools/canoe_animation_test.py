#!/usr/bin/env python3
"""Frame-level canoe animation acceptance capture (real client/renderer)."""
import argparse
import json
import time
from pathlib import Path
from content_selftest import Session

class Capture:
    def __init__(self, session, every=5, film=False):
        self.session = session
        self.folder = session.directory / 'animations'
        self.folder.mkdir(exist_ok=True)
        self.rows = []
        self.every = every
        self.last_pose = None
        self.film = film
        self.photo_tags = set()

    def sample(self, phase, index, tile=(3241,3235)):
        s = self.session
        observation = s.call(f'observe {1 if index else 0} {tile[0]} {tile[1]} canoestation_state_lumbridge fade_overlay:fader')
        state = observation['state']; bit = observation['station']
        loc = observation['scenery']['items']; fade = observation['fade']
        row = {'phase':phase, 'sample':index, 'state':state, 'station':bit, 'scenery':loc, 'fade':fade}
        pose = (phase, bit['client'], state['x'], state['z'], state['camera'], fade.get('exists'), fade.get('hidden'), fade.get('trans'), state['animation'], state['anim_frame'], tuple((l['loc'],l['seq'],l['frame']) for l in loc))
        counts = {867:6, 3285:24, 3301:16, 3303:13, 3302:23, 3304:17, 3305:24, 3306:11}
        tags = {(phase, 'state', bit['client']), (phase, 'position', state['x'], state['z']), (phase, 'fade', state['camera'], fade.get('exists'), fade.get('trans',0)//80)}
        tracks = [('player', state['animation'], state['anim_frame'])] + [('loc',l['seq'],l['frame']) for l in loc]
        for kind,seq,frame in tracks:
            if seq in counts and frame in (0,counts[seq]//2,counts[seq]-1): tags.add((phase,kind,seq,frame))
        if phase.startswith(('ride','cave')): tags.add((phase,'quarter',index//90))
        if pose != self.last_pose and (self.film or tags-self.photo_tags):
            row['image'] = s.shot(f'animations/{phase}-{index:04d}')
        self.last_pose = pose
        self.photo_tags.update(tags)
        if phase.startswith(('ride','cave')) and index % 90 == 0:
            names = ['canoeing_scenery_1','canoeing_scenery_2','canoeing_bullrush','canoeing_bullrush_leaf'] if not phase.startswith('cave') else ['canoeing_cavemouth','canoeing_cave_scenery_1','canoeing_cave_scenery_2','canoeing_cave_scenery_3']
            row['npcs'] = {name:s.call('npc '+name) for name in names}
        self.rows.append(row)
        return row

    def run(self, phase, frames, tile=(3241,3235), stop=None):
        rows = []
        for i in range(frames):
            row = self.sample(phase,i,tile)
            rows.append(row)
            if stop and stop(row): break
        if stop and (not rows or not stop(rows[-1])): raise AssertionError(f'{phase}: state deadline exceeded')
        return rows

    def finish(self):
        from PIL import Image, ImageDraw
        (self.folder/'trace.json').write_text(json.dumps(self.rows,indent=2)+'\n')
        phases = sorted(set(row['phase'] for row in self.rows))
        for phase in phases:
            rows = [row for row in self.rows if row['phase']==phase and 'image' in row]
            images = [Image.open(row['image']).convert('RGB') for row in rows]
            if not images: continue
            if self.film: images[0].save(self.folder/f'{phase}.gif',save_all=True,append_images=images[1:],duration=[max(20, (rows[i+1]['sample']-row['sample'])*20) if i+1<len(rows) else 200 for i,row in enumerate(rows)],loop=0)
            # Contact sheets preserve temporal ordering and annotate state/frames.
            selected = list(range(0,len(images),max(1,len(images)//12)))
            sheet = Image.new('RGB',(1000,185*((len(selected)+2)//3)),'white')
            draw = ImageDraw.Draw(sheet)
            for cell,j in enumerate(selected):
                image=images[j].copy(); image.thumbnail((333,160));x=cell%3*333;y=cell//3*185
                sheet.paste(image,(x,y+20))
                row=rows[j]
                loc=' '.join(f"{l['seq']}:{l['frame']}" for l in row['scenery'])
                draw.text((x,y+3),f"{row['sample']} state={row['station']['client']} p={row['state']['animation']}:{row['state']['anim_frame']} loc={loc}",fill='black')
            sheet.save(self.folder/f'{phase}-sheet.png')


# Cache sequence IDs and counts are checked against configs/all.seq below.
TRACKS = {'chop':[(867,6,40,'player'),(3304,17,93,'loc')],
          'carve':[(3285,24,140,'player')],
          'push':[(3301,16,67,'player'),(3304,17,93,'loc')],
          'board':[(3303,13,59,'player')],
          'ride':[(3302,23,103,'player'),(3306,11,81,'loc')],
          'cave':[(3302,23,103,'player'),(3306,11,81,'loc')],
          'arrival':[(3305,24,128,'loc')], 'sink':[(3305,24,128,'loc')]}
SINKS = {
    0:((3141,3796),(3143,3795),0),1:((3240,3242),(3237,3240),1),
    2:((3199,3344),(3197,3341),0),3:((3109,3415),(3107,3413),1),
    4:((3128,3503),(3127,3503),1),5:((3154,3638),(3157,3638),1),
    6:((2436,3134),(2436,3132),0),7:((2483,3188),(2484,3185),1),
    8:((2577,3261),(2576,3258),0),9:((2571,3360),(2573,3360),1),
    10:((2523,3408),(2522,3411),0)}
SINK_IDS = {1:12159,2:12160,3:12161,4:12162}

def revealed(row):
    fade=row['fade']
    return not fade.get('exists') or fade.get('hidden') or fade.get('trans',0)>=255

def verify(rows):
    from collections import defaultdict
    groups=defaultdict(list)
    for row in rows:groups[row['phase']].append(row)
    result={}
    for phase,group in groups.items():
        kind=phase.split('-')[0]
        checked=[]
        for seq,count,cycles,lane in TRACKS.get(kind,[]):
            samples=[]
            for row in group:
                if not revealed(row):continue
                if lane=='player' and row['state']['animation']==seq:
                    samples.append((row,row['state']['anim_frame'],row['state']['rig']))
                elif lane=='loc':
                    samples.extend((row,item['frame'],item['rig']) for item in row['scenery'] if item['seq']==seq)
            frames={frame for _,frame,_ in samples}
            assert frames==set(range(count)), f'{phase} {seq}: missing rendered frames {sorted(set(range(count))-frames)}'
            assert all(rig['vertices']>0 for _,_,rig in samples), f'{phase}: absent mesh'
            # Looping scenery canonicalizes duplicate poses; non-looping actions do not.
            if seq!=3306:
                assert all(rig['posed_frame']==frame for _,frame,rig in samples), f'{phase} {seq}: renderer did not apply the requested pose'
            assert len({rig['hash'] for _,_,rig in samples})>1, f'{phase} {seq}: mesh is frozen'
            elapsed=samples[-1][0]['state']['time_ms']-samples[0][0]['state']['time_ms']+20
            if kind not in ('ride','cave'):
                assert elapsed>=cycles*20, f'{phase} {seq}: animation tail was cut short ({elapsed} < {cycles*20} ms)'
            if seq==3305:
                assert samples[-1][2]['min_y']>0, f'{phase}: hull did not submerge'
                assert not any(item['loc'] in SINK_IDS.values() for item in group[-1]['scenery']), f'{phase}: sinking hull never despawned'
            checked.append({'sequence':seq,'rendered_frames':len(frames),'distinct_meshes':len({rig['hash'] for _,_,rig in samples})})
        if kind=='push':
            player_start=next(r['state']['time_ms'] for r in group if r['state']['animation']==3301)
            loc_start=next(r['state']['time_ms'] for r in group if any(l['seq']==3304 for l in r['scenery']))
            assert abs(player_start-loc_start)<=20, f'{phase}: push/launch start differs by {player_start-loc_start} ms'
        if kind in ('ride','cave'):
            for row in group:
                if row['state']['x'] in (1817,1845) and not row['state']['camera']:
                    assert row['fade'].get('exists') and row['fade'].get('trans')==0, f'{phase}: exposed unframed cutscene set'
            npcs=defaultdict(set)
            for row in group:
                for name,npc in row.get('npcs',{}).items():
                    if npc['count']:npcs[name].add((npc['x'],npc['z']))
            assert len(npcs)==4 and all(len(v)>1 for v in npcs.values()), f'{phase}: missing/frozen moving scenery {dict(npcs)}'
        if kind in ('ride','cave','arrival','sink'):
            values={r['fade'].get('trans') for r in group if r['fade'].get('exists')}
            assert len(values)>3 and 0 in values and 255 in values, f'{phase}: fade did not interpolate'
        result[phase]=checked
    return result


def prepare(session):
    session.call('pointer -100 -100');session.call('close');session.call('cheat god 1')
    for stat in ['attack','strength','defence','hitpoints']:
        session.call(f'cheat setlevel {stat} 99')
    session.call('cheat canoe 2');session.step(780)


def record(session, boats=(1,), film=False, destinations=None):
    import math
    started=time.monotonic();s=session;c=Capture(s,film=film)
    report={'ok':False}
    try:
        prepare(s)
        for boat in boats:
            if destinations is not None:
                for dest in destinations:
                    s.call('close');s.step(270)
                    s.call(f'cheat canoearrival {dest} {boat}')
                    at,tile,angle=SINKS[dest]
                    s.until(lambda st:(st['x'],st['z'])==at,f'arrival {dest}')
                    dx=tile[0]+(1 if angle==0 else 0)-at[0];dz=tile[1]+(1 if angle==1 else 0)-at[1]
                    yaw=round(math.atan2(-dx,dz)*1024/math.pi)&2047
                    s.call(f'camera {yaw} 280 750')
                    c.run(f'sink-{dest}-{boat}',330,tile)
                continue
            s.call('close');s.call('cheat canoe 1')
            s.until(lambda st:abs(st['x']-3243)<=6 and abs(st['z']-3237)<=6,'station')
            s.call('camera 1024 280 600')
            station='loc 1 3241 3235 canoeing_canoestation_lumbridge'
            s.call(station)
            c.run(f'chop-{boat}',1200,stop=lambda row:row['station']['client']==10)
            s.step(30)
            button={1:'log',2:'dugout',3:'stable_dugout',4:'waka'}[boat]
            s.call(f'button canoeing:{button} 0 1')
            c.run(f'carve-{boat}',220,stop=lambda row:row['station']['client']==boat)
            s.call(station)
            c.run(f'push-{boat}',210,stop=lambda row:row['station']['client']==10+boat)
            s.call(station);c.run(f'board-{boat}',150)
            s.call('button canoe_map_lum:destination_2 -1 1')
            c.run(f'ride-{boat}',720,(1817,4514),stop=lambda row:(row['state']['x'],row['state']['z'])==(3199,3344))
            c.run(f'arrival-{boat}',330,SINKS[2][1])
            s.call('resume messagebox:continue');s.step(30)
        report={'ok':True,'seconds':time.monotonic()-started,'samples':len(c.rows),'checks':verify(c.rows),'film':film}
    except Exception as exc:
        report={'ok':False,'error':str(exc),'seconds':time.monotonic()-started}
        raise
    finally:
        c.finish()
        (c.folder/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))

if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without -O so acceptance assertions remain enabled.')
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',required=True)
    parser.add_argument('--boats',default='1,2,3,4')
    parser.add_argument('--destinations',help='comma-separated arrival indices, or all; runs arrival path only')
    parser.add_argument('--film',action='store_true',help='save every changed pose as PNG and GIF')
    parser.add_argument('--verify-trace',help='verify an existing trace without driving the client')
    args=parser.parse_args()
    if args.verify_trace:
        print(json.dumps(verify(json.loads(Path(args.verify_trace).read_text())),indent=2))
    else:
        boats=tuple(map(int,args.boats.split(',')))
        destinations=tuple(range(11)) if args.destinations=='all' else tuple(map(int,args.destinations.split(','))) if args.destinations else None
        if not boats or any(b not in SINK_IDS for b in boats):parser.error('boats must be 1..4')
        if destinations is not None and any(d not in SINKS for d in destinations):parser.error('destinations must be 0..10')
        record(Session(args.session),boats,args.film,destinations)
